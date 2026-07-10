#pragma once

// Real rbus consumer, independent of the em_cli_bridge/exec() TLS path.
//
// em_cli_bridge.cpp talks to the EasyMesh controller over a TLS socket via
// exec() — that's the "control plane" for reading/writing the
// Device.WiFi.DataElements.* model. This module is a second, separate data
// path: a plain rbus consumer, exactly like `rbuscli` on the box, useful
// for exploring/debugging *any* rbus component's data model — not just
// EasyMesh's — from the same web UI. It was added after discovering on a
// live Filogic gateway that `discelements <path>` fails ("No elements
// discovered!") while `discelements <component>` succeeds, and that the
// EasyMesh data model is actually split across two components (WifiCtrl —
// full schema/template, ~651 elements — and tr_181_service — 5 composite/
// action elements like Topology and SetSSID). See conversation history /
// README for the full manual rbuscli session this was ported from.
//
// Design principle carried over from em_cli_bridge.h: never crash the
// whole REST API just because rbus isn't reachable. Every function here
// checks is_connected() internally and returns a JSON object with an
// "error" field instead of throwing, so /api/v1/rbus/* endpoints degrade
// gracefully (same posture as em_cli_bridge's exec() returning nullptr on
// failure) even if the rbus daemon isn't up yet when this binary starts,
// or the component this binary registers as never got a session.

#include <string>
#include <vector>
#include <cjson/cJSON.h>

namespace em_rbus {

// Opens the rbus consumer handle once at startup (component name
// "onewifi_em_cli_rbus_explorer" — shows up in `discallcomponents` /
// rbus diagnostics like any other consumer). Safe to call even if the
// rbus daemon isn't running; failure is recorded, not thrown, and every
// other function below becomes a fast no-op that reports the error in
// its JSON result instead of crashing the caller.
void init();

// Closes the handle. Call once at shutdown (mirrors WsServer::stop()).
void shutdown();

bool is_connected();
std::string last_error();

// Equivalent of `rbuscli discelements <component>` — the full element
// list a component has registered (schema/template names, with "{i}"
// placeholders for table rows, not populated instance data). Returns
// {"component":..., "elements":[{"name":...,"kind":...}], "count":...}
// or {"component":..., "error":...} on failure. "kind" is a best-effort
// heuristic classification (table/row_property/count/component_root/
// property) — the base rbus_discoverComponentDataElements() call only
// returns names, not element-type metadata, same limitation observed
// with the real rbuscli on-device.
cJSON* discover_elements(const std::string& component);

// Equivalent of the reverse lookup `getnames <path> true` demonstrated
// against WifiCtrl/tr_181_service — which component(s) currently own a
// given dot-path. Returns {"path":..., "components":[...]} or
// {"path":..., "error":...}.
cJSON* discover_components_for_path(const std::string& path);

// Equivalent of `rbuscli getvalues path [path...]` — bulk get. Never
// fails the whole batch for one bad path; each requested path gets its
// own {"name":...,"type":...,"value":...} or {"name":...,"error":...}
// entry inside "results". Top-level "error" is only set for a
// connection-level failure (rbus not reachable at all).
cJSON* get_values(const std::vector<std::string>& paths);

// Equivalent of `rbuscli method_values <method> <param type value>...` /
// `method_noargs <method>` — generic method invoke, for any element that
// turns out to be a method rather than a property (there's no reliable
// way to tell from discover_elements() alone — see the "kind" caveat
// above — so the UI offers both Get and Invoke per element and shows
// whichever the bus actually accepts, same as manually trying commands
// in the rbuscli interactive shell). `params` is a flat JSON object of
// string/number/bool values — sufficient for an explorer/debug tool;
// nested rbus objects as method inputs aren't supported here. Pass
// nullptr for a no-arg method. Returns {"method":...,"status":"success",
// "outParams":[{"name":...,"type":...,"value":...}]} or
// {"method":...,"error":...,"rbus_error_code":...}.
cJSON* invoke_method(const std::string& method_name, cJSON* params);

} // namespace em_rbus
