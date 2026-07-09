#include "ws_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include <cstring>
#include <chrono>
#include <algorithm>

namespace {

struct PerSessionData {
    std::deque<std::string> outbox; // pending messages for this connection
};

// All live connections + their per-session data, protected by one mutex.
// Small connection counts expected on an embedded gateway UI, so a simple
// vector + mutex is the right tool here (no need for lock-free structures).
std::mutex g_conns_mu;
std::vector<struct lws*> g_conns;

} // namespace

WsServer::WsServer(uint16_t port) : port_(port) {}

WsServer::~WsServer() { stop(); }

int WsServer::callback(struct lws* wsi, enum lws_callback_reasons reason,
                        void* user, void* in, size_t len) {
    auto* psd = reinterpret_cast<PerSessionData*>(user);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED: {
            new (psd) PerSessionData();
            {
                std::lock_guard<std::mutex> lock(g_conns_mu);
                g_conns.push_back(wsi);
            }
            lwsl_notice("WebSocket client connected. Total: %zu\n", g_conns.size());

            // Send initial payload, same as the Go handler's conn.WriteJSON(initialData)
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

        case LWS_CALLBACK_SERVER_WRITEABLE: {
            if (!psd->outbox.empty()) {
                const std::string& msg = psd->outbox.front();
                std::vector<unsigned char> buf(LWS_PRE + msg.size());
                memcpy(buf.data() + LWS_PRE, msg.data(), msg.size());
                lws_write(wsi, buf.data() + LWS_PRE, msg.size(), LWS_WRITE_TEXT);
                psd->outbox.pop_front();
                if (!psd->outbox.empty()) lws_callback_on_writable(wsi);
            }
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

        default:
            break;
    }
    return 0;
}

static struct lws_protocols s_protocols[] = {
    { "em-cli-realtime", WsServer::callback, sizeof(PerSessionData), 4096, 0, nullptr, 0 },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 } // terminator
};

bool WsServer::start() {
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = port_;
    info.protocols = s_protocols;
    info.gid = -1;
    info.uid = -1;
    // This lws version's context info doesn't expose a WS-level ping/pong
    // interval directly, so keepalive is handled two ways — same net effect
    // as the Go 25s ping ticker:
    //   1) TCP-level keepalive (ka_time/interval/probes) detects dead
    //      sockets even with no application traffic.
    //   2) The 30s heartbeat broadcast (see start_realtime_background_tasks)
    //      is an application-level "ping" that also carries useful data
    //      (connected_clients count), so it does double duty.
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
