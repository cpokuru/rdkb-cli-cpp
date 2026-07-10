# em_cli_cpp — full C++ port of onewifi_em_cli

Complete real-time EasyMesh controller REST/WebSocket service, ported from
the Go `main.go` in `unified-wifi-mesh/src/rdkb-cli`, targeting a much
smaller memory/binary footprint for the embedded gateway.

## Stack
- **libwebsockets**  — REST **and** real-time WebSocket, both on one port (was gorilla/websocket + net/http sharing one listener via hijack in the Go version; see "Single-port architecture" below for how this works without libmicrohttpd)
- **cJSON**          — JSON (already a dependency in em_cli.cpp / Makefile)
- Direct linkage against `libemcli` via `em_cli_apis.h` — no CGO marshaling

## Single-port architecture (REST + WebSocket + static dashboard, all on :8888)

Earlier iterations of this port used **libmicrohttpd** for REST on `:8888`
and **libwebsockets** for WebSocket on `:8889`, because MHD can't hand a
connection off for a raw protocol upgrade the way Go's `net/http` could
(`gorilla/websocket` hijacks the same TCP connection `net/http` already
accepted). That two-port split broke the frontend's
`new WebSocket(\`ws://${location.host}${apiBase}/ws\`)` call, which
assumes WS lives on the same origin/port as the page it was served from.

The fix: libmicrohttpd is gone. Everything — REST, WebSocket, and the
static dashboard files — now runs through **one libwebsockets context on
one port**, because lws natively handles both plain HTTP request/response
(`LWS_CALLBACK_HTTP*` reasons) and WebSocket upgrade/lifecycle
(`LWS_CALLBACK_ESTABLISHED`/`RECEIVE`/etc) on the same connection type,
in the same callback. This is architecturally the same shape the Go
version had, just via a different library.

**How it works** (`ws_server.cpp`):
- A single `PerSessionData` struct backs both connection types; an
  `is_websocket` flag tags which half is live for a given connection.
- HTTP connections are **single-shot, no keep-alive** — each response
  closes the connection. This sidesteps an entire class of per-session-data
  reuse bugs that HTTP/1.1 pipelining would otherwise introduce (a
  connection reused for a 2nd request on the same `wsi` would need careful
  field-reset logic instead of a clean construct/destruct pair). Given most
  traffic here goes over the persistent WS connection anyway, and REST
  calls are comparatively rare (page loads, config changes), the perf cost
  of a fresh TCP connection per REST call is negligible.
- `handlers_*.cpp` — the ~30 REST handler functions — are **completely
  unchanged** from the two-port version. They're written against a small
  libmicrohttpd-API-compatible shim (`MHD_Connection`, `MHD_HTTP_OK`,
  `send_json()`) defined in `http_server.h`, so swapping the transport
  underneath them didn't require touching handler code — only the shim
  and the server plumbing (`ws_server.cpp`) changed.
- `route()` (in `http_server.cpp`) — the big method+path dispatch table —
  is also unchanged, just exported instead of file-local, since it's now
  called from `ws_server.cpp` instead of an MHD dispatch callback.

**Verified working end-to-end**: REST calls, static file serving (`/` and
`/static/*`), and a live WebSocket connection all confirmed functioning
simultaneously on `:8888` in the same test run, including REST calls still
working correctly *while* a WS connection is active — under
AddressSanitizer, no memory errors.

## Live deployment finding #2: large rbus string values were silently truncated (real bug, now fixed)

Deployed and linking, `/api/v1/rbus/status` showed `connected: true` and the
raw `/api/v1/rbus/get` explorer endpoint returned real data — but
`/api/v1/topology` still came back empty. The raw explorer output revealed
why: the JSON value was cut off mid-string
(`..."TimeStamp":"2026-07-09T23:58` — nothing after it). Root cause:
`rbus_bridge.cpp`'s `add_value_fields()` used `rbusValue_ToString()` into a
fixed 1024-byte stack buffer for *every* value type, including strings.
`Device.WiFi.DataElements.Network.Topology` is a full nested mesh JSON
blob — several KB on a real 3+ device mesh — so it silently truncated mid-
object. The truncated string then failed to parse as JSON in
`get_topology()`, which returned an empty result with no error at all
(matching the "I don't see the real data" symptom exactly — this wasn't a
connectivity or ACL problem, `rbus_get` itself worked fine the whole time).

Fixed: string-typed values now go through `rbusValue_GetString(value,
&len)` instead — a direct pointer + explicit length with no size limit,
the correct extraction path for `RBUS_STRING` values (vs. `ToString`,
which is meant for generic scalar-to-display-string conversion into a
caller-owned buffer, appropriate for a bool/int/etc. but not an
arbitrary-length string). Verified with a 2000+ character test string
(deliberately past the old 1024-byte truncation point) — parses correctly
end-to-end now.



Following the discovery that `Device.WiFi.DataElements.*` is exposed over
**rbus** (RDK's native IPC bus, TR-181 data model) and already working
reliably on the live gateway (`rbuscli` returns real data instantly, no
TLS-socket dependency on the `ieee1905_em_ctrl` controller), the highest-
confidence handlers were migrated off `exec()` entirely:

**Verified rbus function signatures** — a starting `rbus_bridge.cpp` had
already been written against "commonly documented" rbus API signatures.
Same lesson as `em_cli_apis.h`: verified against the real headers (pulled
from `/usr/include/rbus/` on the actual gateway) and found two real bugs:
- `rbus_discoverComponentName`'s output type is `char***` (plain C-string
  array) — the `rbusComponentName` struct used previously doesn't exist
  anywhere in this rbus version's headers.
- `rbus_getExt`'s properties out-param is `rbusProperty_t*` — a single
  opaque handle to the **head of a linked list** (walked via
  `rbusProperty_GetNext()`), not an array indexed with `properties[i]`.
  This one would have compiled fine (both look like double-pointers at a
  glance) but returned garbage or crashed at runtime.

Both fixed in `rbus_bridge.cpp` against the verified signatures.

**New file, `rbus_datamodel_bridge.cpp`** — maps real TR-181 paths
(confirmed via `discelements WifiCtrl`/`discelements tr_181_service` on
the live gateway) to the existing `Device`/`Client`/`HaulConfig`/`TopoNode`
structs, using `rbus_getExt`'s documented wildcard-query support
(`SSID.*.SSID`) for single-round-trip table reads instead of a
`NumberOfEntries` lookup + N-iteration loop:

- **`get_topology()`** — `Device.WiFi.DataElements.Network.Topology` is
  already the exact nested `Device`/`Backhaul`/`RadioList`/`BSSList`/
  `STAList` JSON shape `em_cli_bridge.cpp` used to reconstruct by hand
  from the `exec()` tree. One string get, parse, done — the same
  `buildHaulTypes`-equivalent traversal logic (verified earlier against
  real `topology3.json`) is reused here, just walking a `cJSON*` tree
  instead of `em_network_node_t*`.
- **`get_clients()`** — derived from the *same* Topology fetch (walking
  every `STAList` across every `BSS`) rather than a second round-trip.
- **`get_devices()`** — `Device.{i}.Manufacturer/SerialNumber/
  ManufacturerModel/SoftwareVersion` wildcard-queried, cross-referenced
  against a `get_topology()` call for role (Controller/Agent) since the
  metadata table alone doesn't carry backhaul type.
- **`get_wireless_profiles()`** — `SSID.{i}.SSID/Band/Enable/HaulType`
  wildcard-queried and grouped by `(HaulType, SSID)` — verified end-to-end
  against fake data shaped exactly like the real device's dump (two SSID
  rows, same name, different bands, correctly merged into one profile
  card with `Band: ["2.4GHz","5GHz"]`).

**Devices/clients refresh**: a new 10s background task
(`start_rbus_refresh_task()`) re-fetches `get_devices()`/`get_clients()`
and updates `AppState` — every existing handler that reads `AppState`
(performance tracking, metrics, health score, WS broadcasts) keeps working
unchanged, just backed by real data now instead of the `sample_data.cpp`
fixtures (which stay in place as the initial fallback until the first
successful rbus fetch lands, so the dashboard never shows a blank screen).

**Wireless profiles POST** now calls `Device.WiFi.DataElements.Network.
SetSSID` via `rbusMethod_Invoke` (confirmed as a *method*, not a plain
settable property, via the `tr_181_service` discovery) — the exact input
parameter shape for `SetSSID` hasn't been confirmed against a real
invocation yet, so verify this on one profile before relying on it.

**What's still on the `exec()`/TLS path, and why**: `radios` (channel
config), `wifipolicy`, `wifireset`, `unassoc_sta_query`, and
`controllerIPConfig` remain on `em_cli_bridge.cpp`/`exec()`. The
`discelements` dump provided showed **no policy-related TR-181 elements
anywhere**, and no confirmed path for channel-preference SET — migrating
those without verified evidence would repeat the exact guessing mistake
this whole review process was set up to avoid. `em_cli_bridge.cpp` and
`-lemcli` stay in the build alongside rbus for these specific endpoints.

**rbus explorer** (`/api/v1/rbus/status`, `/rbus/elements`, `/rbus/
components`, `/rbus/get`, `/rbus/invoke`, plus `static/rbus_explorer.html`)
— a generic rbus debug/inspection UI, useful for finding TR-181 paths for
the remaining `exec()`-based endpoints without needing SSH + `rbuscli`
each time. Deploy `rbus_explorer.html` to `/usr/ccsp/EasyMesh/static/`
alongside the other static assets.

**Verified end-to-end** with a fake `librbus` implementation shaped like
the real device's actual data (same two-SSID-row structure, same Topology
JSON nesting): topology, wireless profiles, devices, and clients all
returned correctly through the full REST + rbus stack, including the
grouping logic that merges per-band SSID rows into one profile card.



Deployed against your real `ieee1905_em_ctrl` service, every endpoint that
calls into the em_cli library (`/wireless/profiles`, `/topology`, etc.)
returned HTTP 200 with empty data (`{"haulConfig":[],"total":0}`,
`{"nodes":[],"edges":[]}`) rather than an error — meaning `exec()` was
returning *something*, just not what the handlers expected. Root cause:

The original Go `main()` unconditionally calls `setRemoteIPandPort()` at
startup — reads a persisted controller IP from `/nvram/remoteCtrl.json`,
or falls back to the local outbound interface IP (via a UDP-routing trick:
open a UDP socket toward a well-known external address, no packet
actually sent, then read back which local interface the OS routing table
would have used) — and passes that to `set_remote_addr()`. **This port
never did that at all.** Without it, the library's `is_remote_addr_valid()`
stays false, and `exec()`'s internal connection attempt to the controller
silently degrades to empty results instead of a real connection or a hard
failure — exactly matching the symptom (200 OK, empty payload, no error
logged).

Fixed: added `em::get_local_ip()` (the UDP-routing trick, ported from Go's
`getLocalIP()`) and `em::init_controller_connection()` (persisted-IP-or-
local-IP-fallback, ported from Go's startup sequence) to
`em_cli_bridge.cpp`, called unconditionally at the top of `main()` —
matching the Go version's behavior exactly. Verified: on startup it
correctly detects the outbound interface IP and persists it to
`/nvram/remoteCtrl.json`, matching the original Go binary's own startup
log line ("Connecting with controller IP X and port: 49153").

Also worth knowing for debugging further: your `em_cli.service` unit
redirects stdout to `/tmp/em_cli.log` via `>> /tmp/em_cli.log &`, not to
the systemd journal — so `journalctl -u em_cli.service` will only ever
show libwebsockets' own internal logging (connection accept/close noise),
never this binary's own output or the underlying `libemcli.so`'s
`printf("Command: %s")` tracing (confirmed present in the real
`em_cli.cpp`). If you need to see exec()-level tracing to debug a
specific command, check `/tmp/em_cli.log`, not `journalctl`.

## Verified against the real library source (em_cli.cpp, em_cmd_cli.cpp, and the full inc/ headers)

You provided the actual `em_cli.cpp`, `em_cmd_cli.cpp`, the complete `inc/`
header tree (including the real `em_net_node.h`/`em_base.h`/
`em_cli_apis.h`), and the real `main.go` (6,881 lines, vs. the ~3,000-line
version this was originally built from). This let me verify — not just
infer — the bridge against ground truth:

**One real bug found and fixed**: `em_network_node_t.value_str` is
`em_long_string_t` = `char[128]` in the real `em_base.h` — **not 256
bytes**, which is what the original Go source's own comment claimed
(`(*[256]byte)(unsafe.Pointer(&node.value_str[0]))`) and what this port
had inherited. That Go code was reinterpreting a 128-byte C array as if it
were 256 bytes — harmless only because it never happened to write a
string longer than 128 bytes, but it was one unchecked write away from
overflowing into the adjacent `value_int`/`num_children`/`child[]` fields.
Fixed in `em_cli_bridge.cpp`'s `update_node_value()` and
`update_controller_id()` to use the correct 128-byte bound
(`sizeof(em_long_string_t)`), and verified with a 60-character passphrase
(near WPA's 63-char max) round-tripping correctly with no corruption.

**Everything else confirmed matching, no changes needed**:
- `exec(char* cmd, size_t len, em_network_node_t* node)` signature — exact match
- `get_network_tree_by_key(node, em_long_string_t key)` — the real
  signature takes `em_long_string_t` (`char[128]`), not bare `char*`, but
  since arrays decay to pointers as function parameters this is ABI-identical
  to what the bridge already passes
- `em_network_node_t` struct: `key`/`value_str` are both `char[128]` (not
  a mix of sizes), `type` is a direct enum field (the bridge never assumes
  a particular field layout here since it only ever touches type through
  `get_node_type()`/`set_node_type()`, so this was already safe either way),
  `EM_MAX_DM_CHILDREN` = 64 (matches what was assumed)
- `set_node_type(node, int type)` — takes `int` not the enum type
  directly; harmless since C++ implicitly converts enum values to `int` at
  call sites
- The full real `main.go` route table (all ~45 endpoints) and all 173
  function names (`buildHaulTypes`, `getConfiguredHauls`,
  `getPolicyConfiguration`, `parseRadioList`, `parseSTA`,
  `calculateSignalAtPoint`, every handler and helper) match exactly what
  this port already implements — the extra ~3,800 lines versus the
  originally-provided excerpt is formatting/verbosity, not new
  functionality. No endpoints or handlers are missing at the API-surface level.
- `Device`/`Client`/`DeviceMetrics`/`ClientMetrics` struct field lists
  (and JSON tags) match exactly.

**Confirmed but not yet incorporated**: `execute()` in `em_cmd_cli.cpp`
reveals that `exec()` isn't local processing — it opens a real TLS socket
to a separate EasyMesh controller service (`SSL_write`/`SSL_read` over a
connection from `get_ep_for_dst_svc(ctx, em_service_type_ctrl)`) and reads
the JSON response back from that live connection. This confirms why the
controller-IP configuration (`controllerIPConfig`, `set_remote_addr`) is
load-bearing, not incidental — but it also means `sample_data.cpp`'s
hardcoded devices/clients can only be replaced with real data once this
port is actually running against a reachable controller (can't be
meaningfully tested standalone in a sandbox without one).

## Frontend contract verification (against real script.js/map.js/wireless-settings.js and real topology*.json data)

After the single-port fix, the dashboard loaded but showed gaps. Rather
than guess, I extracted every endpoint the frontend actually calls
(`apiCall(...)` and raw `fetch(...)` call sites across all three JS files)
and cross-checked field-by-field against what this port emits. Findings:

**Confirmed correct** (devices, clients, topology nodes/edges shape,
`/wireless/profiles`, `/wireless/radios` incl. `supported_class`/
`selected_config`/`radio_id`/`radio_index`, `/coverage/analysis`,
`/coverage/analyze` incl. `total_coverage`/`weak_zones`/
`placement_suggestions`) — all match exactly, no changes needed.

**Real gap found and fixed**: topology nodes were missing the `haulTypes`
field entirely — I'd ported the STA-placement logic
(`build_topology_from_device_tree`) but never ported Go's
`buildHaulTypes()`, which groups each node's BSS entries by HaulType
(Fronthaul/Backhaul/Iot) for the topology renderer's SSID/VLAN overlay
circles. Without it, `map.js`'s `d.haulTypes || []` always evaluated to an
empty array, so those circles never rendered. Added `TopoHaulType`/`TopoBSS`
to `models.h`, ported `buildHaulTypes()` into `em_cli_bridge.cpp`
(`build_haul_types()`), and updated the JSON serializer to match the exact
field names/casing the renderer reads (`haul.name`, `haul.ssid`,
`haul.VlanId`, `haul.BSSList[].{BSSID,MLDAddr,vapMode,Band,IEEE,ssid}`).
**Verified against your actual uploaded `topology3.json`** — built a
one-off test harness that parses that real file into the same
`em_network_node_t` tree shape and runs it through the real
`build_topology_from_device_tree()`+serializer path (not a hand-rolled
fixture): correctly produced 5 nodes / 4 edges, with haul types, SSIDs,
VLANs, MLD addresses, and client types (e.g. "Apple iPhone 14",
"Android - Pixel-7-Pro") all matching the source file exactly.

**Two bugs found that are NOT backend issues** — pre-existing in the
frontend JS/HTML itself, present regardless of which backend serves it:
- `script.js`'s `updateOptimizationSuggestions()` calls
  `document.getElementById('optimization-suggestions')`, but `index.html`'s
  actual element has `id="optimization-list"` — the ID doesn't exist, so
  the function silently no-ops and "Loading recommendations..." never
  updates.
- The four health-indicator values (Coverage/Performance/Security/Stability
  `--%` in the Network Health card) have no `id` attribute at all on their
  `<span class="value">` elements in `index.html` — there's no way for any
  JS to target and update them without an HTML edit first.

Neither is fixable from the backend; flag them if/when you're touching the
frontend.

**Minor pre-existing shape mismatch, low impact**: `wireless-settings.js`
assigns `this.advancedSettings.band_steering` directly to a checkbox's
`checked` state, but both this port and the original Go `main.go` model
`band_steering`/`load_balancing` as nested objects
(`{enabled, policy, ...}`), not plain booleans. An object is always
truthy in JS, so those two checkboxes likely always render checked
regardless of actual state — cosmetic, and it's the same in the original
Go backend too (not something this port introduced).

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
- `http_server.cpp` — MHD-API-compatible shim (`send_json`, `send_raw`)
  plus the full method+path routing table (`route()`), static file serving
  for the dashboard, and CORS/validation helpers. Transport-agnostic by
  design — this file has no libwebsockets or networking code in it at all,
  only request handling.
- `ws_server.cpp` — the actual network layer: a single libwebsockets
  context/port handling both HTTP request/response and WebSocket
  lifecycle, real-time broadcast, and the global broadcast accessor. See
  "Single-port architecture" above for the full explanation.

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
`libwebsockets-dev` and `libcjson-dev` are in your target's OpenEmbedded
image recipe (libwebsockets specifically is already used elsewhere in
RDK-B/CCSP, so check if it's already in your rootfs before adding a
duplicate). `libmicrohttpd` is no longer a dependency as of the single-port
architecture change — see above.

## Binary size (dev sandbox, x86_64, dynamically linked, full build,
single-port architecture, before linking libemcli)
- Unstripped: 778 KB
- Stripped:   667 KB

Compare against a stripped Go binary for equivalent functionality, which
typically runs 8-12MB+ even minimal, since the Go runtime/GC/scheduler get
statically baked in regardless of what you use. This is the full ~45-endpoint
build with REST+WS+static-dashboard unified on one port.
