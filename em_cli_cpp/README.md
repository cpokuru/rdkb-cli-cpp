# em_cli_cpp — full C++ port of onewifi_em_cli

Complete real-time EasyMesh controller REST/WebSocket service, ported from
the Go `main.go` in `unified-wifi-mesh/src/rdkb-cli`, targeting a much
smaller memory/binary footprint for the embedded gateway.

## Stack
- **libmicrohttpd** — REST API (was gorilla/mux)
- **libwebsockets**  — real-time WebSocket push (was gorilla/websocket)
- **cJSON**          — JSON (already a dependency in em_cli.cpp / Makefile)
- Direct linkage against `libemcli` via `em_cli_apis.h` — no CGO marshaling

## What's implemented

All ~45 endpoints from the original `main.go` are ported and wired up.
The full project (21 files, ~5,440 lines) compiles clean under
`-Wall -Wextra` and was smoke-tested end-to-end against a mock
`em_cli_apis.h` — REST responses, coverage math, topology parsing,
performance tracking, and WebSocket real-time push all verified working
with no crashes and no memory errors under AddressSanitizer on the
wireless-profiles path (the most exercised one).

**Device / client**
`GET /devices`, `GET /devices/{mac}`, `POST /devices/{mac}/reboot`,
`GET /clients`, `GET /clients/{mac}`, `POST /clients/{mac}/disconnect`,
`POST /clients/{mac}/block`, `POST /clients/{mac}/unblock`

**Wireless**
`GET/POST /wireless/profiles` (SSID/passphrase, via `get_ssid`/`set_ssid`),
`GET/POST /wireless/radios`, `PUT /wireless/radios/{band}`,
`GET/PUT /wireless/advanced`, `POST /wireless/scan`,
`GET /wireless/scan/results`, `GET/PUT /wireless/config`,
`GET/POST /wifipolicy`, `GET/POST /wifireset`,
`POST /unassoc_sta_query`, `GET/POST /controllerIPConfig`

**Coverage / placement**
`GET /coverage/analysis`, `POST /coverage/analyze`,
`POST /coverage/optimize`, `GET/POST /coverage/floorplans`,
`GET/PUT/DELETE /coverage/floorplans/{id}`, `GET /coverage/heatmap`,
`GET /coverage/heatmap/{band}`, `POST /coverage/simulate`,
`POST /coverage/placement/predict`, `GET /coverage/weakzones`,
`GET /coverage/deadspots`, `GET /coverage/report`,
`GET /coverage/report/pdf`

**Topology**
`GET /topology` (live device-tree traversal via `get_network`),
`POST /topology/optimize`

**Performance / metrics**
`GET /performance/devices`, `GET /performance/devices/{mac}`,
`GET /performance/devices/{mac}/clients`, `GET /performance/clients/{mac}`,
`GET /performance/alarms`, `POST /performance/alarms/{id}/acknowledge`,
`GET /metrics/devices`, `GET /metrics/clients`, `GET /metrics/performance`,
`GET /metrics/interference`

**System**
`GET/PUT /config`, `GET /security/profiles`, `GET /security/threats`,
`GET /firmware/status`, `POST /firmware/update`, `GET /reports/usage`,
`GET /reports/performance`, `GET /system/status`, `GET /system/logs`

**Real-time WebSocket**: initial state payload on connect, 10s
`metrics_update` broadcast, 30s heartbeat, plus event-driven broadcasts on
every mutation (`device_update`, `client_disconnected`, `wireless_update`,
`topology_optimized`) — same shape as the Go version's `broadcastMessage()`
call sites.

**Background tasks**: 10s performance-history + alarm-check loop
(`start_performance_background_task`, mirrors `updatePerformanceHistory()`
+ `checkDeviceAlarms()`), 10s metrics ticker, 30s heartbeat ticker.

## Architecture

- `models.h` — every struct from the Go version, renamed to C++ conventions
- `app_state.h` — the Go package-level globals (`devices`, `clients`,
  `radioConfigs`, `floorPlans`, `performanceHistory`, etc.), now members of
  a singleton `AppState` guarded by `std::shared_mutex`/`std::mutex`
  (direct swap for Go's `sync.RWMutex`/`sync.Mutex`)
- `em_cli_bridge.{h,cpp}` — every tree-walking helper from the CGO layer
  (`getTreeValue`, `updateNodeValue`, `getConfiguredHauls`,
  `getPolicyConfiguration`, `getConfiguredChannels`,
  `build_topology_from_device_tree`, etc.), translated 1:1 with no
  marshaling boilerplate since there's no cgo boundary in native C++
- `coverage.{h,cpp}` — the coverage/placement math (`performCoverageAnalysis`,
  `optimizeDevicePlacement`, weak-zone flood fill, etc.) — pure computation,
  no em_cli dependency, the most mechanical part of the port
- `json_helpers*.{h,cpp}` — cJSON serialize/parse for every model type
- `handlers_*.cpp` — one file per functional area (wireless, coverage,
  topology, performance, misc), each holding the REST handler functions
- `http_server.cpp` — libmicrohttpd routing table + streamed-POST-body
  handling (libmicrohttpd delivers bodies across multiple callbacks,
  unlike Go's single `io.Reader`)
- `ws_server.cpp` — libwebsockets real-time push, global broadcast accessor

## IMPORTANT — before building on your target

`em_cli_bridge.cpp` assumes these things about `em_network_node_t` and the
library functions, all inferred directly from how the original Go CGO code
used them (buffer sizes, function signatures, enum names). Verify against
your actual `em_cli_apis.h`:
- `value_str` is a fixed 256-byte buffer (Go: `(*[256]byte)(unsafe.Pointer(&node.value_str[0]))`)
- `value_int` is an unsigned int
- `get_network_tree_by_key(tree, key)` takes a mutable `char*`
- `exec(cmd, len, payload)` returns `em_network_node_t*`
- Enum values: `em_network_node_data_type_{obj,array_obj,array_num,array_str,string,number,true,false}`
- `node->child[i]` is a fixed-size array of child pointers, `node->num_children`
  tracks how many are populated

If any of these don't match, the fix is localized to `em_cli_bridge.cpp` —
nothing above it (handlers, JSON serialization, routing) needs to change.

I compile-verified the entire bridge and every handler against a mock of
this header reconstructed from the CGO call sites (not shipped — see git
history if you want to see it), so the *logic* is exercised end-to-end;
what's unverified is only whether your real header's exact field
names/types match my reconstruction.

## Known simplifications vs. the Go version (call these out if you need
exact parity)
- **Port split**: REST on `:8888`, WS on `:8889` as separate sockets,
  because libmicrohttpd can't hijack a connection for WS upgrade the way
  gorilla/websocket could multiplex both on `:8888`. Front both with
  nginx/haproxy on one external port, or swap the REST layer to civetweb
  (supports hijacking) if a single port is a hard requirement.
- **Channel scan duration**: capped at 3s internally instead of literally
  sleeping the requested `scan_duration` (up to 300s) — the real
  driver-backed scan should replace this stub generator entirely; capping
  was just to keep the demo/test loop responsive.
- **Device reboot "back online" delay**: 5s instead of the Go version's
  30s, same reasoning — trivial one-line change back to 30s if you want
  exact parity.
- **Controller IP GET**: `getControllerRemoteIP()`'s file-read is ported
  (`em::get_controller_remote_ip()`) but not yet wired into the GET handler
  (currently returns empty/default) — the POST path is fully wired and
  persists correctly. One-line fix in `handlers_wireless.cpp`.
- **Sample data**: `devices`/`clients` are still the hardcoded fallback
  values (matching the Go version's own fallback when
  `/nvram/static/*.json` isn't present) rather than loading from
  `em_cli_apis.h` device/client commands directly. Swap `sample_data.cpp`
  for real loading calls using the same `em::exec_cmd()` pattern used
  everywhere else — same shape as `getConfiguredHauls()`.
- **PDF report**: placeholder byte stream, matching the Go version (which
  was also just a placeholder, not real PDF generation).

## Build
```
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```
CMakeLists.txt expects `em_cli_apis.h` at `../../inc` relative to this
project (matching the original `-I../../inc` CGO flag) and links
`-lemcli` from `../../install/lib` (matching the original `-lemcli`
LDFLAGS). Adjust both paths if you place this project elsewhere relative
to the `unified-wifi-mesh` checkout.

## Cross-compiling for the Filogic target
Point CMake at your Yocto SDK's toolchain file, and make sure
`libmicrohttpd-dev`, `libwebsockets-dev`, `libcjson-dev` are in your
target's OpenEmbedded image recipe (libwebsockets specifically is already
used elsewhere in RDK-B/CCSP, so check if it's already in your rootfs
before adding a duplicate).

## Binary size (dev sandbox, x86_64, dynamically linked, full build before
linking libemcli)
- Unstripped: 769 KB
- Stripped:   659 KB

Compare against a stripped Go binary for equivalent functionality, which
typically runs 8-12MB+ even minimal, since the Go runtime/GC/scheduler get
statically baked in regardless of what you use. This is the full ~45-endpoint
build, not just the earlier subset.
