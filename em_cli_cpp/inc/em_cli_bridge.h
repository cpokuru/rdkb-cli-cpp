#pragma once

// This pulls in your existing C library header directly — the whole reason
// the CGO layer in main.go needed C.CString()/C.free() pairs everywhere was
// to marshal Go strings across the cgo boundary. In C++ there is no such
// boundary: em_network_node_t* is a plain C struct pointer we can pass
// around like anything else, and std::string vs C string conversion only
// has to happen once, right where we call into the library, not throughout
// every helper function.
extern "C" {
#include "em_cli_apis.h"
}

#include <string>
#include <vector>
#include <utility>
#include "models.h"

namespace em {

// ===== Tree read helpers (replace getTreeValue / getKeyIntValue) =====

// Equivalent of Go's getTreeValue(tree, key) — returns "", "true", or
// "false" for string/bool node types, "" if the key isn't found.
std::string get_tree_value(em_network_node_t* tree, const std::string& key);

// Equivalent of Go's getKeyIntValue(tree, key).
int get_key_int_value(em_network_node_t* tree, const std::string& key);

// Direct child lookup by key, wraps get_network_tree_by_key with a
// std::string key instead of a manually managed C.CString.
em_network_node_t* get_child(em_network_node_t* tree, const std::string& key);

// ===== Tree write helpers (replace updateNodeValue / updateNodeBool / etc.) =====

void update_node_value(em_network_node_t* parent, const std::string& key, const std::string& new_val);
void update_node_bool(em_network_node_t* parent, const std::string& key, bool enabled);
void update_node_int(em_network_node_t* parent, const std::string& key, int val);
void update_node_array(em_network_node_t* parent, const std::string& key, const std::string& array_str);

// ===== Command execution (replaces C.exec(cmd, strlen(cmd), node)) =====

// Runs an em_cli command against the running mesh (e.g. "get_ssid
// OneWifiMesh", "set_channel OneWifiMesh"). Returns the resulting tree, or
// nullptr on failure — caller does not own strings, only the returned tree
// (free with free_network_tree if it's not part of an existing tree you
// already own).
em_network_node_t* exec_cmd(const std::string& cmd, em_network_node_t* payload = nullptr);

// ===== Domain-specific parsing (replaces getConfiguredHauls / getPolicyConfiguration) =====

// Walks a "NetworkSSIDList" node exactly like the Go getConfiguredHauls()
// did, producing the same HaulConfig list.
std::vector<HaulConfig> get_configured_hauls(em_network_node_t* tree);

// Pushes an updated HaulConfig back into the tree, matching
// updateNetworkSSIDList()'s field-by-field update logic.
bool update_network_ssid_list(em_network_node_t* network_ssid_tree, const HaulConfig& haul,
                                bool is_wireless_profile_update);

// Applies a pending SSID/passphrase change, matching applyNetworkNameConfig().
bool apply_network_name_config(em_network_node_t* ssid_tree);

// MAC/normalize helpers (matches isValidMac / normalizeBandsArray in main.go)
bool is_valid_mac(const std::string& mac);
std::string normalize_bands_array(const std::vector<std::string>& bands);
bool is_all_ff(const std::string& mac);
std::string normalize_mac_array_string(const std::vector<std::string>& macs);
std::vector<std::string> parse_mac_list(em_network_node_t* node);
std::string map_channels_to_slice(const std::vector<int>& channels);

// Grows/shrinks parent's children to exactly new_count entries, cloning
// from a template row (matches reconcileChildrenToCount in main.go). Used
// by the policy update handlers when a list needs to change length.
int reconcile_children_to_count(em_network_node_t* parent, int new_count);

// ===== Wireless policy (replaces getPolicyConfiguration / updatePolicySettings) =====

std::vector<WifiPolicyConfig> get_policy_configuration(em_network_node_t* device_list_tree);
bool update_policy_settings(em_network_node_t* device_list_tree, const WifiPolicyConfig& config);
bool apply_wifi_policy_config(em_network_node_t* policy_tree);

// ===== Channel / radio config (replaces getChannelCapabilityFromTree / getConfiguredChannels) =====

// band -> list of (class, channel list) capability entries
std::vector<std::pair<int, std::vector<ClassInfo>>> get_channel_capability_from_tree(em_network_node_t* tree);
std::vector<WifiChannelConfig> get_configured_channels(em_network_node_t* tree);
bool update_anticipated_channel_preference(em_network_node_t* tree, const std::vector<ChannelConfigEntry>& updated);
bool apply_channel_config(em_network_node_t* tree);

// ===== WiFi reset (replaces WifiResetHandler helpers) =====

bool update_controller_id(em_network_node_t* reset_tree, const std::string& selected_mac);
bool apply_reset_config(em_network_node_t* reset_tree);
std::vector<std::string> get_interface_preference(em_network_node_t* tree);

// ===== Topology (replaces loadTopologyFromDeviceTree's traverse) =====

struct TopologyResult {
    std::vector<TopoNode> nodes;
    std::vector<TopoEdge> edges;
};
TopologyResult build_topology_from_device_tree(em_network_node_t* device_node);

// ===== Controller remote IP config (replaces setRemoteIPandPort / getControllerRemoteIP) =====

// Validates the IP, converts to little-endian, calls set_remote_addr, and
// persists to /nvram/remoteCtrl.json — same as the Go version. Returns
// false if the IP/port fail validation.
bool set_remote_addr_and_persist(const std::string& ip, unsigned int port);

// Reads back /nvram/remoteCtrl.json. Returns {ip, port} — ip empty if the
// file doesn't exist yet (matches the Go fallback behavior).
std::pair<std::string, unsigned int> get_controller_remote_ip();

} // namespace em
