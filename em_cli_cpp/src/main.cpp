/*
  If not stated otherwise in this file or this component's LICENSE file the
  following copyright and licenses apply:

  Copyright 2023 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/
#include <cstdio>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

#include "ws_server.h"
#include "app_state.h"
#include "handlers.h"

void seed_sample_data(); // src/sample_data.cpp — replaced by em_cli data load in next batch

static std::atomic<bool> g_running{true};

static void handle_sigint(int) { g_running = false; }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    seed_sample_data();
    init_wireless_and_coverage_defaults();

    // Single server on port 8888: libwebsockets handles plain HTTP requests
    // (LWS_CALLBACK_HTTP) and WebSocket upgrades on the same port — the same
    // architecture as the original Go net/http + gorilla/websocket version.
    WsServer ws(8888);
    if (!ws.start()) {
        fprintf(stderr, "Failed to start HTTP + WebSocket server on port 8888\n");
        return 1;
    }
    set_global_ws_server(&ws);

    start_realtime_background_tasks(ws);
    start_performance_background_task();

    printf("EasyMesh R6 Controller (C++) running\n");
    printf("HTTP + WebSocket: http://0.0.0.0:8888\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    printf("Shutting down...\n");
    ws.stop();
    return 0;
}
