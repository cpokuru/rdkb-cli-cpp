#pragma once

// The actual data-model replacement for em_cli_bridge.cpp's exec()/TLS
// path — per the "full replace" decision, every REST handler that used to
// call em::exec_cmd() now calls one of these instead. Same posture as
// rbus_bridge.cpp: never throw, degrade to empty/error JSON if rbus isn't
// reachable, and keep the function signatures here stable even if the
// TR-181 paths underneath need adjusting later.
//
// Path reference (confirmed via `rbuscli discelements WifiCtrl` /
// `discelements tr_181_service` on the real gateway):
//   Device.WiFi.DataElements.Network.Topology        — single string,
//       already the exact nested Device/Backhaul/RadioList/BSSList/STAList
//       JSON shape em_cli_bridge.cpp used to reconstruct by hand from the
//       exec() tree. This is the biggest win of the rbus path — devices,
//       radios, BSS, and STAs (clients) all come from ONE get.
//   Device.WiFi.DataElements.Network.SSID.{i}.*       — wireless profiles
//       table (SSID, Band, Enable, HaulType — no PassPhrase exposed here,
//       see get_wireless_profiles()'s doc comment).
//   Device.WiFi.DataElements.Network.Device.{i}.*     — per-device
//       metadata table (Manufacturer, SerialNumber, ManufacturerModel,
//       SoftwareVersion) that ISN'T in the Topology blob, cross-referenced
//       by ID against the Topology tree for status/role.

#include <string>
#include <vector>
#include "models.h"

namespace em_rbus {

struct TopologyResult {
    std::vector<TopoNode> nodes;
    std::vector<TopoEdge> edges;
};

// Single rbus get on Device.WiFi.DataElements.Network.Topology, parsed
// directly into the same TopoNode/TopoEdge shape the existing topology
// JSON serializer (topo_node_to_json/topo_edge_to_json) already expects —
// no changes needed to handlers_topology.cpp's response building, only to
// where the tree comes from.
TopologyResult get_topology();

// Derived from the same Topology fetch as get_topology() (STAList entries
// across every BSS) rather than a second bus round-trip — matches
// em_cli_bridge.cpp's old build_sta_list() in spirit, just sourced from
// the JSON string instead of the em_network_node_t tree.
std::vector<Client> get_clients();

// Device.WiFi.DataElements.Network.Device.{i}.* metadata table
// (Manufacturer/SerialNumber/ManufacturerModel/SoftwareVersion/
// BackhaulMediaType), cross-referenced against a fresh get_topology() call
// for status/role/backhaul info the metadata table alone doesn't carry.
std::vector<Device> get_devices();

// Device.WiFi.DataElements.Network.SSID.{i}.* wireless profile table.
// NOTE: PassPhrase does not appear anywhere in the discovered SSID.{i}.*
// element list (confirmed via discelements) — TR-181 doesn't expose
// stored passphrases over a plain get, for the obvious security reason.
// pass_phrase comes back empty; the frontend's profile cards should treat
// an empty PassPhrase as "unchanged / not shown", not "cleared".
std::vector<HaulConfig> get_wireless_profiles();

// Device.WiFi.DataElements.Network.Topology already carries per-radio
// Band/Channel/Class/Enabled data (confirmed in a real dump: "ID":
// "02:02:10:b6:52:1c", "Enabled": true, "Band": 0, "Class": 32,
// "Channel": 6) — derived from the same already-verified Topology fetch
// used by get_topology()/get_clients(), rather than a separate multi-level
// wildcard query (Device.*.Radio.*.CurrentOperatingClasses.*.Class) with
// no confirmed evidence rbus supports nesting wildcards that deep on this
// build. NOTE: per-channel *capabilities* (supported operating classes,
// max TX power) are NOT in the Topology blob, so RadioConfig.device_list's
// supported_class stays empty here — only the current/selected config is
// real. If Radio.{i}.Capabilities.OperatingClasses.* turns out reachable
// via rbus later (verify with the explorer), wire that in for capabilities.
std::vector<RadioConfig> get_radios();

} // namespace em_rbus
