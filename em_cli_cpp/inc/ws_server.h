#pragma once

#include <libwebsockets.h>
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <thread>
#include <atomic>

// Real-time push layer AND REST transport — both now run through this one
// libwebsockets context/port, matching the Go version's original
// architecture (net/http + gorilla/websocket sharing one listening
// socket via connection hijack). libwebsockets' single callback handles
// both HTTP request/response reasons (LWS_CALLBACK_HTTP*) and WebSocket
// lifecycle reasons (LWS_CALLBACK_ESTABLISHED/RECEIVE/etc) on the same
// connection type, which is what makes single-port dual-protocol possible
// here the way it isn't with libmicrohttpd (MHD can't hand a connection
// off for a raw protocol upgrade the way lws or Go's net/http can).
//
// Mirrors the Go version's:
//   - websocketHandler() -> upgrade + track connection
//   - TCP keepalive (ka_time/interval/probes) in place of a WS ping ticker
//   - broadcastMessage() -> fan-out to all connected clients
//   - 10s metrics updater ticker
//   - 30s heartbeat ticker
//   - the entire net/http request/response cycle -> route() in http_server.cpp
//
// libwebsockets is single-threaded per event loop by design (this is what
// keeps it fast/low-memory), so outbound broadcasts are queued and flushed
// via lws_callback_on_writable, rather than written directly from other
// threads — the concurrency-safe equivalent of Go pushing into each
// connection's channel.
class WsServer {
public:
    explicit WsServer(uint16_t port);
    ~WsServer();

    bool start();  // spins up the lws event loop on its own thread
    void stop();

    // Thread-safe: queue a JSON message for broadcast to all connected
    // clients. Equivalent to Go's broadcastMessage(map[string]interface{}).
    void broadcast(const std::string& json_message);

    size_t connection_count();

    // Public because it's wired into the file-scope lws_protocols table
    // (libwebsockets' C API needs a plain function pointer here).
    static int callback(struct lws* wsi, enum lws_callback_reasons reason,
                         void* user, void* in, size_t len);

private:
    uint16_t port_;
    struct lws_context* context_ = nullptr;
    std::thread loop_thread_;
    std::atomic<bool> running_{false};

    void run_loop();
};

// Convenience: start background threads matching the Go tickers
// (startMetricsUpdater @10s, startWebSocketBroadcaster @30s heartbeat).
void start_realtime_background_tasks(WsServer& ws);

// Global accessor so mutation handlers (device reboot, config update, etc.)
// can broadcast without threading a WsServer* through every function
// signature — mirrors how main.go's broadcastMessage() just closed over
// the package-level wsConnections slice.
void set_global_ws_server(WsServer* ws);
WsServer* global_ws_server();
