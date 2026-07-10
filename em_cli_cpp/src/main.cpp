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
#include "rbus_bridge.h"

void seed_sample_data(); // src/sample_data.cpp — initial fallback fixtures,
                          // overwritten by the first successful rbus fetch

static std::atomic<bool> g_running{true};

static void handle_sigint(int) { g_running = false; }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    std::signal(SIGINT, handle_sigint);
    std::signal(SIGTERM, handle_sigint);

    seed_sample_data();
    init_wireless_and_coverage_defaults();

    // Full replace of the em_cli/exec()/TLS-controller path with rbus —
    // opens the consumer handle once at startup (component name
    // "onewifi_em_cli_rbus_explorer"). Every REST handler that used to
    // call em::exec_cmd() now goes through rbus_datamodel_bridge.cpp
    // instead, which itself calls into rbus_bridge.cpp's verified
    // rbus_open/rbus_getExt/rbusMethod_Invoke wrappers.
    em_rbus::init();

    // REST + WebSocket + static dashboard files, all on :8888 — matches
    // the Go version's single ListenAndServe("0.0.0.0:8888") exactly,
    // since both protocols now run through the same libwebsockets context
    // (see ws_server.cpp for how the HTTP/WS split is handled per-connection).
    WsServer server(8888);
    if (!server.start()) {
        fprintf(stderr, "Failed to start server on port 8888\n");
        return 1;
    }
    set_global_ws_server(&server);

    start_realtime_background_tasks(server);
    start_performance_background_task();
    start_rbus_refresh_task();

    printf("EasyMesh R6 Controller (C++) running\n");
    printf("REST API + WebSocket + dashboard: http://0.0.0.0:8888\n");
    printf("rbus: %s\n", em_rbus::is_connected() ? "connected" : em_rbus::last_error().c_str());

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    printf("Shutting down...\n");
    em_rbus::shutdown();
    server.stop();
    return 0;
}
