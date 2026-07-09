#include "ws_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "http_server.h"
#include <cstring>
#include <chrono>
#include <algorithm>
#include <vector>

namespace {

// Single struct backs both connection types; `is_websocket` tags which
// half is live. Connections are single-shot (no HTTP keep-alive — see the
// close-after-response note in LWS_CALLBACK_HTTP_WRITEABLE below), so
// LWS_CALLBACK_HTTP/LWS_CALLBACK_ESTABLISHED each fire exactly once before
// the matching CLOSED reason, making placement-new/destroy here safe
// without needing to guard against reuse mid-connection.
struct PerSessionData {
    bool is_websocket = false;

    // ----- WebSocket state -----
    std::deque<std::string> outbox;

    // ----- HTTP state -----
    std::string method;
    std::string path;
    std::string body;
    MHD_Connection conn;
    bool response_ready = false;
    bool headers_sent = false;
    size_t body_offset = 0;
};

std::mutex g_conns_mu;
std::vector<struct lws*> g_conns;

const char* method_name(int lws_method) {
    switch (lws_method) {
        case LWSHUMETH_GET: return "GET";
        case LWSHUMETH_POST: return "POST";
        case LWSHUMETH_OPTIONS: return "OPTIONS";
        case LWSHUMETH_PUT: return "PUT";
        case LWSHUMETH_PATCH: return "PATCH";
        case LWSHUMETH_DELETE: return "DELETE";
        case LWSHUMETH_HEAD: return "HEAD";
        default: return "GET";
    }
}

// Writes the queued WS message at the front of the outbox, matching the
// original per-connection outbound queue behavior.
void write_ws_message(struct lws* wsi, PerSessionData* psd) {
    if (psd->outbox.empty()) return;
    const std::string& msg = psd->outbox.front();
    std::vector<unsigned char> buf(LWS_PRE + msg.size());
    memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
    lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
    psd->outbox.pop_front();
    if (!psd->outbox.empty()) lws_callback_on_writable(wsi);
}

// Writes the HTTP response headers (once) and then the body, chunked if
// needed so large payloads (e.g. coverage heatmaps) can't silently
// truncate on a short lws_write(). Returns -1 to signal "close the
// connection now" (either on completion or on error), 0 to keep going.
int write_http_response(struct lws* wsi, PerSessionData* psd) {
    if (!psd->headers_sent) {
        uint8_t header_buf[LWS_PRE + 2048];
        uint8_t* start = &header_buf[LWS_PRE];
        uint8_t* p = start;
        uint8_t* end = &header_buf[sizeof(header_buf) - 1];

        if (lws_add_http_common_headers(wsi, psd->conn.response_status,
                psd->conn.response_content_type.c_str(),
                psd->conn.response_body.size(), &p, end)) {
            return -1;
        }
        // Matches the original Go corsMiddleware.
        if (lws_add_http_header_by_name(wsi, (const unsigned char*)"Access-Control-Allow-Origin:",
                                         (const unsigned char*)"*", 1, &p, end)) return -1;
        {
            static const char* methods_val = "GET, POST, PUT, DELETE, OPTIONS";
            if (lws_add_http_header_by_name(wsi, (const unsigned char*)"Access-Control-Allow-Methods:",
                                             (const unsigned char*)methods_val,
                                             (int)strlen(methods_val), &p, end)) return -1;
        }
        {
            static const char* headers_val = "Content-Type, Authorization";
            if (lws_add_http_header_by_name(wsi, (const unsigned char*)"Access-Control-Allow-Headers:",
                                             (const unsigned char*)headers_val,
                                             (int)strlen(headers_val), &p, end)) return -1;
        }
        if (!psd->conn.extra_header_name.empty()) {
            std::string hname = psd->conn.extra_header_name + ":";
            if (lws_add_http_header_by_name(wsi, (const unsigned char*)hname.c_str(),
                                             (const unsigned char*)psd->conn.extra_header_value.c_str(),
                                             (int)psd->conn.extra_header_value.size(), &p, end)) return -1;
        }
        if (lws_finalize_write_http_header(wsi, start, &p, end)) {
            return -1;
        }
        psd->headers_sent = true;

        if (psd->conn.response_body.empty()) {
            return -1; // nothing more to send, done
        }
        lws_callback_on_writable(wsi);
        return 0;
    }

    constexpr size_t MAX_CHUNK = 8192;
    size_t remain = psd->conn.response_body.size() - psd->body_offset;
    size_t chunk = std::min(remain, MAX_CHUNK);
    bool is_final = (psd->body_offset + chunk) >= psd->conn.response_body.size();

    std::vector<unsigned char> buf(LWS_PRE + chunk);
    memcpy(buf.data() + LWS_PRE, psd->conn.response_body.data() + psd->body_offset, chunk);
    int n = lws_write(wsi, buf.data() + LWS_PRE, chunk, is_final ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP);
    if (n < 0) return -1;
    psd->body_offset += static_cast<size_t>(n);

    if (psd->body_offset >= psd->conn.response_body.size()) {
        return -1; // fully sent, close (see close-after-response note above)
    }
    lws_callback_on_writable(wsi);
    return 0;
}

} // namespace

WsServer::WsServer(uint16_t port) : port_(port) {}

WsServer::~WsServer() { stop(); }

int WsServer::callback(struct lws* wsi, enum lws_callback_reasons reason,
                        void* user, void* in, size_t len) {
    auto* psd = reinterpret_cast<PerSessionData*>(user);

    switch (reason) {

        // ===== WebSocket lifecycle (unchanged from the two-port version) =====
        case LWS_CALLBACK_ESTABLISHED: {
            new (psd) PerSessionData();
            psd->is_websocket = true;
            {
                std::lock_guard<std::mutex> lock(g_conns_mu);
                g_conns.push_back(wsi);
            }
            lwsl_notice("WebSocket client connected. Total: %zu\n", g_conns.size());

            auto& state = AppState::instance();
            CJsonPtr root(cJSON_CreateObject());
            add_str(root.get(), "type", "initial");
            {
                std::shared_lock lock(state.devices_mu);
                cJSON* arr = cJSON_CreateArray();
                for (auto& d : state.devices) cJSON_AddItemToArray(arr, device_to_json(d));
                cJSON_AddItemToObject(root.get(), "devices", arr);
            }
            {
                std::shared_lock lock(state.clients_mu);
                cJSON* arr = cJSON_CreateArray();
                for (auto& c : state.clients) cJSON_AddItemToArray(arr, client_to_json(c));
                cJSON_AddItemToObject(root.get(), "clients", arr);
            }
            add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
            psd->outbox.push_back(to_json_string(root.get()));
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_RECEIVE:
            // Client doesn't send data in this protocol (mirrors the Go
            // read loop, which only exists to detect close/errors).
            break;

        case LWS_CALLBACK_CLOSED: {
            psd->~PerSessionData();
            std::lock_guard<std::mutex> lock(g_conns_mu);
            g_conns.erase(std::remove(g_conns.begin(), g_conns.end(), wsi), g_conns.end());
            lwsl_notice("WebSocket client disconnected. Total: %zu\n", g_conns.size());
            break;
        }

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (psd->is_websocket) write_ws_message(wsi, psd);
            break;
        }

        // ===== HTTP lifecycle (new — this is what makes single-port
        // REST+WS possible without libmicrohttpd) =====
        case LWS_CALLBACK_HTTP: {
            new (psd) PerSessionData();
            psd->is_websocket = false;

            char* uri_ptr = nullptr;
            int uri_len = 0;
            int meth = lws_http_get_uri_and_method(wsi, &uri_ptr, &uri_len);
            psd->method = method_name(meth);
            psd->path = (uri_len > 0 && uri_ptr) ? std::string(uri_ptr, uri_len) : "/";

            // Strip query string — the router doesn't use it.
            auto qpos = psd->path.find('?');
            if (qpos != std::string::npos) psd->path = psd->path.substr(0, qpos);

            // No Content-Length means no body coming — dispatch immediately
            // rather than waiting for a BODY_COMPLETION that won't arrive
            // for e.g. plain GETs.
            if (lws_hdr_total_length(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH) == 0) {
                route(&psd->conn, psd->method, psd->path, psd->body);
                psd->response_ready = true;
                lws_callback_on_writable(wsi);
            }
            break;
        }

        case LWS_CALLBACK_HTTP_BODY: {
            if (in && len > 0) psd->body.append(reinterpret_cast<const char*>(in), len);
            break;
        }

        case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
            if (!psd->response_ready) {
                route(&psd->conn, psd->method, psd->path, psd->body);
                psd->response_ready = true;
            }
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_HTTP_WRITEABLE: {
            if (psd->is_websocket || !psd->response_ready) break;
            return write_http_response(wsi, psd);
        }

        case LWS_CALLBACK_CLOSED_HTTP: {
            if (!psd->is_websocket) psd->~PerSessionData();
            break;
        }

        default:
            break;
    }
    return 0;
}

static struct lws_protocols s_protocols[] = {
    { "em-cli-unified", WsServer::callback, sizeof(PerSessionData), 8192, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 } // terminator
};

bool WsServer::start() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port_;
    info.protocols = s_protocols;
    info.gid = -1;
    info.uid = -1;
    // TCP-level keepalive stands in for a WS-level ping/pong interval
    // (this lws version's context info doesn't expose one directly) and
    // detects dead sockets even with no application traffic. The 30s
    // heartbeat broadcast (start_realtime_background_tasks) is an
    // application-level "ping" that also carries useful data
    // (connected_clients count), so it does double duty for WS clients.
    info.ka_time = 20;
    info.ka_probes = 3;
    info.ka_interval = 5;
    info.options = LWS_SERVER_OPTION_DISABLE_IPV6;

    context_ = lws_create_context(&info);
    if (!context_) return false;

    running_ = true;
    loop_thread_ = std::thread(&WsServer::run_loop, this);
    return true;
}

void WsServer::run_loop() {
    while (running_) {
        lws_service(context_, 50 /* ms */);
    }
}

void WsServer::stop() {
    running_ = false;
    if (loop_thread_.joinable()) loop_thread_.join();
    if (context_) {
        lws_context_destroy(context_);
        context_ = nullptr;
    }
}

void WsServer::broadcast(const std::string& json_message) {
    std::lock_guard<std::mutex> lock(g_conns_mu);
    for (auto* wsi : g_conns) {
        auto* psd = reinterpret_cast<PerSessionData*>(lws_wsi_user(wsi));
        if (psd) {
            psd->outbox.push_back(json_message);
            lws_callback_on_writable(wsi);
        }
    }
    // Wake the service loop immediately rather than waiting for the next
    // poll tick, so broadcasts feel instant (this is the "realtime" part).
    if (context_) lws_cancel_service(context_);
}

size_t WsServer::connection_count() {
    std::lock_guard<std::mutex> lock(g_conns_mu);
    return g_conns.size();
}

void start_realtime_background_tasks(WsServer& ws) {
    // Metrics updater: every 10s, same cadence as Go's startMetricsUpdater().
    std::thread([&ws]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            CJsonPtr msg(cJSON_CreateObject());
            add_str(msg.get(), "type", "metrics_update");
            add_str(msg.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
            ws.broadcast(to_json_string(msg.get()));
        }
    }).detach();

    // Heartbeat: every 30s, same cadence as Go's startWebSocketBroadcaster().
    std::thread([&ws]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            CJsonPtr msg(cJSON_CreateObject());
            add_str(msg.get(), "type", "heartbeat");
            add_str(msg.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
            cJSON_AddNumberToObject(msg.get(), "connected_clients", (double)ws.connection_count());
            ws.broadcast(to_json_string(msg.get()));
        }
    }).detach();
}

namespace {
WsServer* g_ws_server = nullptr;
}

void set_global_ws_server(WsServer* ws) { g_ws_server = ws; }
WsServer* global_ws_server() { return g_ws_server; }
