#include "em_cli_bridge.h"
#include <cstring>
#include <regex>
#include <algorithm>
#include <functional>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cjson/cJSON.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace em {

// NOTE: get_network_tree_by_key()'s key parameter type depends on your
// actual em_cli_apis.h signature (the Go CGO binding used C.CString(key),
// which cgo generates as *C.char / char*). If your header declares it as
// `const char*` this const_cast is unnecessary but harmless; if it's a
// plain `char*` (as the Go binding implies), this is required since the
// library doesn't take ownership or mutate it in practice.
static inline char* ckey(const std::string& s) {
    return const_cast<char*>(s.c_str());
}

std::string get_tree_value(em_network_node_t* tree, const std::string& key) {
    em_network_node_t* node = get_network_tree_by_key(tree, ckey(key));
    if (node) {
        switch (get_node_type(node)) {
            case em_network_node_data_type_string:
                return std::string(node->value_str);
            case em_network_node_data_type_false:
                return "false";
            case em_network_node_data_type_true:
                return "true";
            default:
                break;
        }
    }
    return "";
}

int get_key_int_value(em_network_node_t* tree, const std::string& key) {
    em_network_node_t* node = get_network_tree_by_key(tree, ckey(key));
    if (node && get_node_type(node) == em_network_node_data_type_number) {
        return static_cast<int>(node->value_int);
    }
    return 0;
}

em_network_node_t* get_child(em_network_node_t* tree, const std::string& key) {
    return get_network_tree_by_key(tree, ckey(key));
}

void update_node_value(em_network_node_t* parent, const std::string& key, const std::string& new_val) {
    em_network_node_t* node = get_network_tree_by_key(parent, ckey(key));
    if (!node) return;
    // CORRECTED against the real em_base.h: value_str is em_long_string_t
    // = char[128], NOT 256 as the Go source's comment claimed
    // (buf := (*[256]byte)(unsafe.Pointer(&node.value_str[0]))). That Go
    // code was reinterpreting a 128-byte C array as if it were 256 bytes —
    // harmless in practice only because it never wrote a string longer
    // than 128 bytes, but it was one un-checked write away from
    // overflowing into value_int/num_children/child[]. Using the real
    // size here closes that off properly rather than inheriting the bug.
    constexpr size_t buf_size = sizeof(em_long_string_t); // 128, per em_base.h's em_network_node_t.value_str type
    std::memset(node->value_str, 0, buf_size);
    std::memcpy(node->value_str, new_val.data(), std::min(new_val.size(), buf_size - 1));
}

void update_node_bool(em_network_node_t* parent, const std::string& key, bool enabled) {
    em_network_node_t* node = get_network_tree_by_key(parent, ckey(key));
    if (!node) return;
    set_node_type(node, enabled ? em_network_node_data_type_true : em_network_node_data_type_false);
}

void update_node_int(em_network_node_t* parent, const std::string& key, int val) {
    em_network_node_t* node = get_network_tree_by_key(parent, ckey(key));
    if (!node) return;
    node->value_int = static_cast<unsigned int>(val);
}

void update_node_array(em_network_node_t* parent, const std::string& key, const std::string& array_str) {
    em_network_node_t* node = get_network_tree_by_key(parent, ckey(key));
    if (!node) return;
    set_node_type(node, em_network_node_data_type_array_str);
    node->num_children = 0;
    set_node_array_value(node, ckey(array_str));
}

em_network_node_t* exec_cmd(const std::string& cmd, em_network_node_t* payload) {
    return exec(ckey(cmd), cmd.size(), payload);
}

std::vector<HaulConfig> get_configured_hauls(em_network_node_t* tree) {
    std::vector<HaulConfig> hauls;
    em_network_node_t* list_node = get_child(tree, "NetworkSSIDList");
    if (!list_node) return hauls;

    for (int i = 0; i < static_cast<int>(list_node->num_children); i++) {
        em_network_node_t* node = list_node->child[i];
        if (!node) continue;

        em_network_node_t* haul_type_node = get_child(node, "HaulType");
        if (!haul_type_node || haul_type_node->num_children == 0) continue;

        HaulConfig cfg;
        cfg.haul_type = std::string(haul_type_node->child[0]->value_str);
        cfg.enabled = (get_tree_value(node, "Enable") == "true");

        em_network_node_t* band_obj = get_child(node, "Band");
        if (!band_obj || band_obj->num_children == 0) continue;
        for (int b = 0; b < static_cast<int>(band_obj->num_children); b++) {
            cfg.bands.push_back(std::string(band_obj->child[b]->value_str) + "GHz");
        }

        cfg.security_type = get_tree_value(node, "AuthType");
        cfg.vlan_id = get_key_int_value(node, "VLANID");
        cfg.ssid = get_tree_value(node, "SSID");
        cfg.pass_phrase = get_tree_value(node, "PassPhrase");

        hauls.push_back(std::move(cfg));
    }
    return hauls;
}

bool update_network_ssid_list(em_network_node_t* network_ssid_tree, const HaulConfig& haul,
                                bool is_wireless_profile_update) {
    em_network_node_t* ssid_list_node = get_child(network_ssid_tree, "NetworkSSIDList");
    if (!ssid_list_node) return false;

    for (int i = 0; i < static_cast<int>(ssid_list_node->num_children); i++) {
        em_network_node_t* item = ssid_list_node->child[i];
        if (!item) continue;

        em_network_node_t* haul_node = get_child(item, "HaulType");
        if (!haul_node || haul_node->num_children == 0) continue;

        std::string haul_type_str(haul_node->child[0]->value_str);
        if (haul_type_str.find(haul.haul_type) != std::string::npos) {
            update_node_value(item, "SSID", haul.ssid);
            update_node_value(item, "PassPhrase", haul.pass_phrase);
            if (is_wireless_profile_update) {
                update_node_value(item, "AuthType", haul.security_type);
                update_node_bool(item, "Enable", haul.enabled);
                update_node_int(item, "VLANID", haul.vlan_id);
                update_node_array(item, "Band", normalize_bands_array(haul.bands));
            }
        }
    }
    return true;
}

bool apply_network_name_config(em_network_node_t* ssid_tree) {
    em_network_node_t* ssid_node = get_child(ssid_tree, "Result");
    if (!ssid_node) return false;
    exec_cmd("set_ssid OneWifiMesh", ssid_node);
    return true;
}

bool is_valid_mac(const std::string& mac_in) {
    // Strip interface suffix the same way Go's isValidMac did
    // (strings.Split(mac, " ")[0]).
    std::string mac = mac_in.substr(0, mac_in.find(' '));
    static const std::regex mac_re("^([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}$");
    return std::regex_match(mac, mac_re);
}

std::string normalize_bands_array(const std::vector<std::string>& bands) {
    // Same de-dup + fixed sort order (2.4, 5, 6) as Go's normalizeBandsArray.
    bool has24 = false, has5 = false, has6 = false;
    for (const auto& b : bands) {
        if (b == "2.4GHz") has24 = true;
        else if (b == "5GHz") has5 = true;
        else if (b == "6GHz") has6 = true;
    }
    std::vector<std::string> normalized;
    if (has24) normalized.push_back("2.4");
    if (has5) normalized.push_back("5");
    if (has6) normalized.push_back("6");

    std::string out = "[";
    for (size_t i = 0; i < normalized.size(); i++) {
        if (i > 0) out += ", ";
        out += normalized[i];
    }
    out += "]";
    return out;
}

bool is_all_ff(const std::string& mac_in) {
    static const std::regex non_hex("[^a-fA-F0-9]");
    std::string hex = std::regex_replace(mac_in, non_hex, "");
    // trim whitespace already implied by regex strip
    if (hex.size() != 10 && hex.size() != 12) return false;
    for (char c : hex) if (c != 'F' && c != 'f') return false;
    return true;
}

std::string normalize_mac_array_string(const std::vector<std::string>& macs) {
    std::vector<std::string> seen;
    for (auto mac : macs) {
        for (auto& c : mac) c = std::toupper(static_cast<unsigned char>(c));
        // trim
        size_t start = mac.find_first_not_of(" \t");
        size_t end = mac.find_last_not_of(" \t");
        if (start == std::string::npos) continue;
        mac = mac.substr(start, end - start + 1);
        if (mac.empty() || mac == "00:00:00:00:00:00") continue;
        if (std::find(seen.begin(), seen.end(), mac) == seen.end()) seen.push_back(mac);
    }
    std::sort(seen.begin(), seen.end());
    std::string out = "[";
    for (size_t i = 0; i < seen.size(); i++) {
        if (i > 0) out += ", ";
        out += seen[i];
    }
    out += "]";
    return out;
}

std::vector<std::string> parse_mac_list(em_network_node_t* node) {
    std::vector<std::string> macs;
    if (!node) return macs;
    for (int i = 0; i < static_cast<int>(node->num_children); i++) {
        em_network_node_t* elem = node->child[i];
        if (!elem) continue;
        std::string mac(elem->value_str);
        if (!mac.empty() && mac != "00:00:00:00:00:00") macs.push_back(mac);
    }
    return macs;
}

std::string map_channels_to_slice(const std::vector<int>& channels) {
    if (channels.empty()) return "[]";
    std::string out = "[";
    for (size_t i = 0; i < channels.size(); i++) {
        if (i > 0) out += ", ";
        out += std::to_string(channels[i]);
    }
    out += "]";
    return out;
}

int reconcile_children_to_count(em_network_node_t* parent, int new_count) {
    if (!parent) return 0;
    int current = static_cast<int>(parent->num_children);

    if (current < new_count) {
        for (int i = current; i < new_count; i++) {
            em_network_node_t* clone_tree = clone_network_tree_for_display(parent, nullptr, 0xffff, false);
            if (!clone_tree || clone_tree->num_children == 0 || !clone_tree->child[0]) break;
            parent->child[i] = clone_tree->child[0];
            parent->num_children++;
        }
        current = static_cast<int>(parent->num_children);
    }

    if (current > new_count) {
        for (int i = current - 1; i >= new_count; i--) {
            if (parent->child[i]) {
                free_network_tree(parent->child[i]);
                parent->child[i] = nullptr;
            }
            parent->num_children--;
        }
        current = static_cast<int>(parent->num_children);
    }

    return current;
}

// ===== Wireless policy =====

std::vector<WifiPolicyConfig> get_policy_configuration(em_network_node_t* device_list_tree) {
    std::vector<WifiPolicyConfig> configs;
    if (!device_list_tree) return configs;

    for (int i = 0; i < static_cast<int>(device_list_tree->num_children); i++) {
        em_network_node_t* device_node = device_list_tree->child[i];
        if (!device_node) continue;

        em_network_node_t* policy_node = get_child(device_node, "Policy");
        if (!policy_node) return configs;

        em_network_node_t* ap_metric = get_child(policy_node, "AP Metrics Reporting Policy");
        if (!ap_metric) return configs;

        WifiPolicyConfig cfg;
        cfg.id = get_tree_value(device_node, "ID");
        cfg.ap_metric_reporting_policy.interval = get_key_int_value(ap_metric, "Interval");
        cfg.ap_metric_reporting_policy.managed_client_marker = get_tree_value(ap_metric, "Managed Client Marker");

        if (em_network_node_t* local_steering = get_child(policy_node, "Local Steering Disallowed Policy")) {
            cfg.local_steering_disallowed = parse_mac_list(get_child(local_steering, "Disallowed STA"));
        }
        if (em_network_node_t* btm_steering = get_child(policy_node, "BTM Steering Disallowed Policy")) {
            cfg.btm_steering_disallowed = parse_mac_list(get_child(btm_steering, "Disallowed STA"));
        }

        em_network_node_t* channel_scan = get_child(policy_node, "Channel Scan Reporting Policy");
        if (!channel_scan) return configs;
        cfg.report_independent_channel_scans = get_key_int_value(channel_scan, "Report Independent Channel Scans");

        em_network_node_t* dot1q = get_child(policy_node, "Default 802.1Q Settings Policy");
        if (!dot1q) return configs;
        cfg.default_8021q_settings_policy.primary_vlan_id = get_key_int_value(dot1q, "Primary VLAN ID");
        cfg.default_8021q_settings_policy.default_pcp = get_key_int_value(dot1q, "Default PCP");

        if (em_network_node_t* unsucc = get_child(policy_node, "Unsuccessful Association Policy")) {
            cfg.unsuccessful_assoc_policy.report_unsuccess_assoc =
                (get_tree_value(unsucc, "Report Unsuccessful Associations") == "true");
            cfg.unsuccessful_assoc_policy.max_reporting_rate = get_key_int_value(unsucc, "Maximum Reporting Rate");
        }

        if (em_network_node_t* backhaul = get_child(policy_node, "Backhaul BSS Configuration Policy")) {
            for (int j = 0; j < static_cast<int>(backhaul->num_children); j++) {
                em_network_node_t* child = backhaul->child[j];
                if (!child) continue;
                std::string bssid = get_tree_value(child, "BSSID");
                std::transform(bssid.begin(), bssid.end(), bssid.begin(), ::toupper);
                if (bssid.empty() || bssid == "00:00:00:00:00:00") continue;
                BackhaulBSSConfig entry;
                entry.bssid = bssid;
                entry.profile1_bsta_disallowed = (get_tree_value(child, "Profile-1 bSTA Disallowed") == "true");
                entry.profile2_bsta_disallowed = (get_tree_value(child, "Profile-2 bSTA Disallowed") == "true");
                cfg.backhaul_bss_config_policy.push_back(entry);
            }
        }

        if (em_network_node_t* qos = get_child(policy_node, "QoS Management Policy")) {
            if (em_network_node_t* mscs = get_child(qos, "MSCS Disallowed STA List")) {
                cfg.qos_management_policy.mscs_disallowed_sta_list = parse_mac_list(mscs);
            }
            if (em_network_node_t* scs = get_child(qos, "SCS Disallowed STA List")) {
                cfg.qos_management_policy.scs_disallowed_sta_list = parse_mac_list(scs);
            }
        }

        if (em_network_node_t* radio_metric = get_child(policy_node, "Radio Specific Metrics Policy")) {
            for (int j = 0; j < static_cast<int>(radio_metric->num_children); j++) {
                em_network_node_t* c = radio_metric->child[j];
                if (!c) continue;
                RadioSpecificMetrics rm;
                rm.id = get_tree_value(c, "ID");
                rm.sta_rcpi_threshold = get_key_int_value(c, "STA RCPI Threshold");
                rm.sta_rcpi_hysteresis = get_key_int_value(c, "STA RCPI Hysteresis");
                rm.ap_utilization_threshold = get_key_int_value(c, "AP Utilization Thresold");
                rm.sta_traffic_stats = get_key_int_value(c, "STA Traffic Stats");
                rm.sta_link_metrics = get_key_int_value(c, "STA Link Metrics");
                rm.sta_status = get_key_int_value(c, "STA Status");
                cfg.radio_specific_metrics_policy.push_back(rm);
            }
        }

        if (em_network_node_t* radio_steering = get_child(policy_node, "Radio Steering Parameters")) {
            for (int j = 0; j < static_cast<int>(radio_steering->num_children); j++) {
                em_network_node_t* c = radio_steering->child[j];
                if (!c) continue;
                RadioSteeringParameters rs;
                rs.id = get_tree_value(c, "ID");
                rs.steering_policy = get_key_int_value(c, "Steering Policy");
                rs.utilization_threshold = get_key_int_value(c, "Utilization Threshold");
                rs.rcpi_threshold = get_key_int_value(c, "RCPI Threshold");
                cfg.radio_steering_parameters_policy.push_back(rs);
            }
        }

        configs.push_back(std::move(cfg));
    }
    return configs;
}

bool update_policy_settings(em_network_node_t* device_list_tree, const WifiPolicyConfig& policy_config) {
    if (!device_list_tree) return false;

    for (int i = 0; i < static_cast<int>(device_list_tree->num_children); i++) {
        em_network_node_t* device_node = device_list_tree->child[i];
        if (!device_node) continue;
        if (get_tree_value(device_node, "ID") != policy_config.id) continue;

        em_network_node_t* policy_node = get_child(device_node, "Policy");
        if (!policy_node) return false;

        if (em_network_node_t* ap_metric = get_child(policy_node, "AP Metrics Reporting Policy")) {
            update_node_int(ap_metric, "Interval", policy_config.ap_metric_reporting_policy.interval);
            update_node_value(ap_metric, "Managed Client Marker", policy_config.ap_metric_reporting_policy.managed_client_marker);
        }

        if (em_network_node_t* local_steering = get_child(policy_node, "Local Steering Disallowed Policy")) {
            update_node_array(local_steering, "Disallowed STA", normalize_mac_array_string(policy_config.local_steering_disallowed));
        }
        if (em_network_node_t* btm_steering = get_child(policy_node, "BTM Steering Disallowed Policy")) {
            update_node_array(btm_steering, "Disallowed STA", normalize_mac_array_string(policy_config.btm_steering_disallowed));
        }

        if (em_network_node_t* channel_scan = get_child(policy_node, "Channel Scan Reporting Policy")) {
            update_node_int(channel_scan, "Report Independent Channel Scans", policy_config.report_independent_channel_scans);
        }

        if (em_network_node_t* dot1q = get_child(policy_node, "Default 802.1Q Settings Policy")) {
            update_node_int(dot1q, "Primary VLAN ID", policy_config.default_8021q_settings_policy.primary_vlan_id);
            update_node_int(dot1q, "Default PCP", policy_config.default_8021q_settings_policy.default_pcp);
        }

        if (em_network_node_t* unsucc = get_child(policy_node, "Unsuccessful Association Policy")) {
            update_node_bool(unsucc, "Report Unsuccessful Associations", policy_config.unsuccessful_assoc_policy.report_unsuccess_assoc);
            update_node_int(unsucc, "Maximum Reporting Rate", policy_config.unsuccessful_assoc_policy.max_reporting_rate);
        }

        if (em_network_node_t* backhaul = get_child(policy_node, "Backhaul BSS Configuration Policy")) {
            for (auto& entry : policy_config.backhaul_bss_config_policy) {
                if (entry.bssid.empty() || entry.bssid == "00:00:00:00:00:00") continue;
                std::string req_bssid = entry.bssid;
                std::transform(req_bssid.begin(), req_bssid.end(), req_bssid.begin(), ::toupper);
                for (int j = 0; j < static_cast<int>(backhaul->num_children); j++) {
                    em_network_node_t* child = backhaul->child[j];
                    if (!child) continue;
                    std::string tree_bssid = get_tree_value(child, "BSSID");
                    std::transform(tree_bssid.begin(), tree_bssid.end(), tree_bssid.begin(), ::toupper);
                    if (tree_bssid == req_bssid) {
                        update_node_bool(child, "Profile-1 bSTA Disallowed", entry.profile1_bsta_disallowed);
                        update_node_bool(child, "Profile-2 bSTA Disallowed", entry.profile2_bsta_disallowed);
                        break;
                    }
                }
            }
        }

        if (em_network_node_t* qos = get_child(policy_node, "QoS Management Policy")) {
            if (em_network_node_t* mscs = get_child(qos, "MSCS Disallowed STA List")) {
                update_node_array(mscs, "MSCS Disallowed STA List", normalize_mac_array_string(policy_config.qos_management_policy.mscs_disallowed_sta_list));
            }
            if (em_network_node_t* scs = get_child(qos, "SCS Disallowed STA List")) {
                update_node_array(scs, "SCS Disallowed STA List", normalize_mac_array_string(policy_config.qos_management_policy.scs_disallowed_sta_list));
            }
        }

        if (em_network_node_t* radio_metric = get_child(policy_node, "Radio Specific Metrics Policy");
            radio_metric && !policy_config.radio_specific_metrics_policy.empty()) {
            reconcile_children_to_count(radio_metric, static_cast<int>(policy_config.radio_specific_metrics_policy.size()));
            for (size_t j = 0; j < policy_config.radio_specific_metrics_policy.size(); j++) {
                em_network_node_t* child = radio_metric->child[j];
                if (!child) continue;
                auto& e = policy_config.radio_specific_metrics_policy[j];
                update_node_value(child, "ID", e.id);
                update_node_int(child, "STA RCPI Threshold", e.sta_rcpi_threshold);
                update_node_int(child, "STA RCPI Hysteresis", e.sta_rcpi_hysteresis);
                update_node_int(child, "AP Utilization Thresold", e.ap_utilization_threshold);
                update_node_int(child, "STA Traffic Stats", e.sta_traffic_stats);
                update_node_int(child, "STA Link Metrics", e.sta_link_metrics);
                update_node_int(child, "STA Status", e.sta_status);
            }
        }

        if (em_network_node_t* radio_steering = get_child(policy_node, "Radio Steering Parameters");
            radio_steering && !policy_config.radio_steering_parameters_policy.empty()) {
            reconcile_children_to_count(radio_steering, static_cast<int>(policy_config.radio_steering_parameters_policy.size()));
            for (size_t j = 0; j < policy_config.radio_steering_parameters_policy.size(); j++) {
                em_network_node_t* child = radio_steering->child[j];
                if (!child) continue;
                auto& e = policy_config.radio_steering_parameters_policy[j];
                update_node_value(child, "ID", e.id);
                update_node_int(child, "Steering Policy", e.steering_policy);
                update_node_int(child, "Utilization Threshold", e.utilization_threshold);
                update_node_int(child, "RCPI Threshold", e.rcpi_threshold);
            }
        }
    }
    return true;
}

bool apply_wifi_policy_config(em_network_node_t* policy_tree) {
    em_network_node_t* set_policy_node = get_child(policy_tree, "Result");
    if (!set_policy_node) return false;
    exec_cmd("set_policy OneWifiMesh", set_policy_node);
    return true;
}

// ===== Channel / radio config =====

std::vector<std::pair<int, std::vector<ClassInfo>>> get_channel_capability_from_tree(em_network_node_t* tree) {
    std::vector<std::pair<int, std::vector<ClassInfo>>> result;
    if (!tree || tree->num_children == 0) return result;

    auto find_or_add = [&](int band) -> std::vector<ClassInfo>& {
        for (auto& p : result) if (p.first == band) return p.second;
        result.push_back({band, {}});
        return result.back().second;
    };

    for (int i = 0; i < static_cast<int>(tree->num_children); i++) {
        em_network_node_t* item = tree->child[i];
        if (!item) continue;
        em_network_node_t* radio_list = get_child(item, "RadioList");
        if (!radio_list || radio_list->num_children == 0) continue;

        for (int r = 0; r < static_cast<int>(radio_list->num_children); r++) {
            em_network_node_t* radio = radio_list->child[r];
            if (!radio) continue;
            em_network_node_t* cap_node = get_child(radio, "ChannelCapability");
            if (!cap_node || cap_node->num_children == 0) continue;

            for (int j = 0; j < static_cast<int>(cap_node->num_children); j++) {
                em_network_node_t* cap_child = cap_node->child[j];
                int band_val = get_key_int_value(cap_child, "Band");
                int class_val = get_key_int_value(cap_child, "Class");

                auto& existing = find_or_add(band_val);
                bool dup = false;
                for (auto& c : existing) if (c.cls == class_val) { dup = true; break; }
                if (dup) continue;

                std::vector<int> non_operable;
                if (em_network_node_t* non_op = get_child(cap_child, "NonOperable")) {
                    for (int k = 0; k < static_cast<int>(non_op->num_children); k++)
                        non_operable.push_back(static_cast<int>(non_op->child[k]->value_int));
                }

                std::vector<int> channel_list;
                if (em_network_node_t* ch_list = get_child(cap_child, "ChannelList")) {
                    for (int k = 0; k < static_cast<int>(ch_list->num_children); k++) {
                        int ch = static_cast<int>(ch_list->child[k]->value_int);
                        if (std::find(non_operable.begin(), non_operable.end(), ch) != non_operable.end()) continue;
                        channel_list.push_back(ch);
                    }
                }

                ClassInfo ci; ci.cls = class_val; ci.supported_channels = channel_list;
                existing.push_back(ci);
            }
        }
    }
    return result;
}

std::vector<WifiChannelConfig> get_configured_channels(em_network_node_t* tree) {
    std::vector<WifiChannelConfig> result;

    em_network_node_t* acp_node = get_child(tree, "AnticipatedChannelPreference");
    if (!acp_node) return result;
    em_network_node_t* device_list_node = get_child(tree, "DeviceList");
    if (!device_list_node) return result;

    // GLOBAL entry
    {
        std::vector<ChannelConfigEntry> global_cfgs;
        for (int idx = 0; idx < static_cast<int>(acp_node->num_children); idx++) {
            em_network_node_t* cfg_node = acp_node->child[idx];
            if (!cfg_node) continue;
            int band_index = (idx == 2) ? idx + 1 : idx;

            ChannelConfigEntry e;
            e.radio_index = band_index;
            e.cls = get_key_int_value(cfg_node, "Class");
            if (em_network_node_t* ch_list = get_child(cfg_node, "ChannelList"))
                for (int k = 0; k < static_cast<int>(ch_list->num_children); k++)
                    e.channels.push_back(static_cast<int>(ch_list->child[k]->value_int));
            if (em_network_node_t* pref_list = get_child(cfg_node, "ChannelPrefList"))
                for (int k = 0; k < static_cast<int>(pref_list->num_children); k++)
                    e.preferences.push_back(static_cast<int>(pref_list->child[k]->value_int));
            global_cfgs.push_back(e);
        }
        WifiChannelConfig global;
        global.device_id = "FF:FF:FF:FF:FF:FF";
        global.selected_config = global_cfgs;
        result.push_back(global);
    }

    // per-device entries
    for (int i = 0; i < static_cast<int>(device_list_node->num_children); i++) {
        em_network_node_t* device_node = device_list_node->child[i];
        if (!device_node) continue;
        std::string device_id = get_tree_value(device_node, "ID");

        em_network_node_t* radio_list_node = get_child(device_node, "RadioList");
        if (!radio_list_node || radio_list_node->num_children == 0) continue;

        std::vector<ChannelConfigEntry> cfgs;
        for (int j = 0; j < static_cast<int>(radio_list_node->num_children); j++) {
            em_network_node_t* radio_node = radio_list_node->child[j];
            if (!radio_node) continue;
            std::string radio_id = get_tree_value(radio_node, "ID");
            int band_index = get_key_int_value(radio_node, "Band");

            ChannelConfigEntry base; base.radio_id = radio_id; base.radio_index = band_index;
            cfgs.push_back(base);

            em_network_node_t* radio_channel_node = get_child(radio_node, "AnticipatedChannelPreference");
            if (!radio_channel_node) continue;

            for (int idx = 0; idx < static_cast<int>(radio_channel_node->num_children); idx++) {
                em_network_node_t* cfg_node = radio_channel_node->child[idx];
                if (!cfg_node) continue;
                ChannelConfigEntry e;
                e.radio_id = radio_id; e.radio_index = band_index;
                e.cls = get_key_int_value(cfg_node, "Class");
                if (em_network_node_t* ch_list = get_child(cfg_node, "ChannelList"))
                    for (int k = 0; k < static_cast<int>(ch_list->num_children); k++)
                        e.channels.push_back(static_cast<int>(ch_list->child[k]->value_int));
                if (em_network_node_t* pref_list = get_child(cfg_node, "ChannelPrefList"))
                    for (int k = 0; k < static_cast<int>(pref_list->num_children); k++)
                        e.preferences.push_back(static_cast<int>(pref_list->child[k]->value_int));
                cfgs.push_back(e);
            }
        }

        WifiChannelConfig wc; wc.device_id = device_id; wc.selected_config = cfgs;
        result.push_back(wc);
    }

    return result;
}

bool update_anticipated_channel_preference(em_network_node_t* tree, const std::vector<ChannelConfigEntry>& updated) {
    if (!tree || updated.empty()) return false;

    em_network_node_t* channel_pref_tree = get_child(tree, "AnticipatedChannelPreference");
    if (!channel_pref_tree) return false;

    for (auto& cfg : updated) {
        if (is_all_ff(cfg.device_id)) {
            if (cfg.radio_index < 0 || cfg.radio_index >= static_cast<int>(channel_pref_tree->num_children)) continue;
            em_network_node_t* channel_pref_node = channel_pref_tree->child[cfg.radio_index];
            if (em_network_node_t* class_node = get_child(channel_pref_node, "Class"))
                class_node->value_int = static_cast<unsigned int>(cfg.cls);
            update_node_array(channel_pref_node, "ChannelList", map_channels_to_slice(cfg.channels));
            update_node_array(channel_pref_node, "ChannelPrefList", map_channels_to_slice(cfg.preferences));
        } else {
            bool found = false;
            em_network_node_t* device_list_node = get_child(tree, "DeviceList");
            if (!device_list_node) return false;

            for (int i = 0; i < static_cast<int>(device_list_node->num_children) && !found; i++) {
                em_network_node_t* device_node = device_list_node->child[i];
                if (!device_node) continue;
                if (get_tree_value(device_node, "ID") != cfg.device_id) continue;

                em_network_node_t* radio_list_node = get_child(device_node, "RadioList");
                if (!radio_list_node) continue;

                for (int j = 0; j < static_cast<int>(radio_list_node->num_children); j++) {
                    em_network_node_t* radio_node = radio_list_node->child[j];
                    if (!radio_node || get_tree_value(radio_node, "ID") != cfg.radio_id) continue;

                    em_network_node_t* radio_channel_node = get_child(radio_node, "AnticipatedChannelPreference");
                    if (!radio_channel_node) continue;

                    if (radio_channel_node->num_children < 1) {
                        em_network_node_t* clone_tree = clone_network_tree_for_display(channel_pref_tree, nullptr, 0xffff, false);
                        if (!clone_tree || clone_tree->num_children == 0) break;
                        radio_channel_node->child[0] = clone_tree->child[cfg.radio_index];
                        radio_channel_node->num_children = 1;
                    }
                    em_network_node_t* channel_pref_node = radio_channel_node->child[0];
                    if (em_network_node_t* class_node = get_child(channel_pref_node, "Class"))
                        class_node->value_int = static_cast<unsigned int>(cfg.cls);
                    update_node_array(channel_pref_node, "ChannelList", map_channels_to_slice(cfg.channels));
                    update_node_array(channel_pref_node, "ChannelPrefList", map_channels_to_slice(cfg.preferences));
                    found = true;
                    break;
                }
            }
        }
    }
    return true;
}

bool apply_channel_config(em_network_node_t* tree) {
    em_network_node_t* result_node = get_child(tree, "Result");
    if (!result_node) return false;
    exec_cmd("set_channel OneWifiMesh", result_node);
    return true;
}

// ===== WiFi reset =====

bool update_controller_id(em_network_node_t* reset_tree, const std::string& selected_mac) {
    if (!is_valid_mac(selected_mac)) return false;
    em_network_node_t* node = get_child(reset_tree, "ControllerID");
    if (!node) return false;
    std::memset(node->value_str, 0, sizeof(em_long_string_t));
    std::memcpy(node->value_str, selected_mac.data(), std::min(selected_mac.size(), sizeof(em_long_string_t) - 1));
    return true;
}

bool apply_reset_config(em_network_node_t* reset_tree) {
    em_network_node_t* reset_node = get_child(reset_tree, "wfa-dataelements:Reset");
    if (!reset_node) return false;
    exec_cmd("reset OneWifiMesh", reset_node);
    return true;
}

std::vector<std::string> get_interface_preference(em_network_node_t* tree) {
    std::vector<std::string> macs;
    if (!tree) return macs;
    // Leaf string node
    if (tree->num_children == 0) {
        std::string v(tree->value_str);
        if (!v.empty()) macs.push_back(v);
        return macs;
    }
    for (int i = 0; i < static_cast<int>(tree->num_children); i++) {
        auto child_macs = get_interface_preference(tree->child[i]);
        macs.insert(macs.end(), child_macs.begin(), child_macs.end());
    }
    return macs;
}

// ===== Topology =====

// Parses one radio's BSSList into (band, BSS-details) pairs — shared by
// both build_sta_list (client placement) and build_haul_types (SSID/VLAN
// overlay circles), so the tree is only walked once per node.
struct RadioBssInfo {
    int band = 0;
    std::string ieee;
    std::vector<TopoBSS> bss;      // full BSS records, for haulTypes
    std::vector<TopoSTA> stas;     // associated STAs only, for STA placement
};

static std::vector<RadioBssInfo> parse_radio_list_full(em_network_node_t* radio_list_tree) {
    std::vector<RadioBssInfo> radios;
    if (!radio_list_tree) return radios;

    for (int i = 0; i < static_cast<int>(radio_list_tree->num_children); i++) {
        em_network_node_t* radio = radio_list_tree->child[i];
        if (!radio) continue;

        RadioBssInfo r;
        r.band = get_key_int_value(radio, "Band");
        r.ieee = get_tree_value(radio, "IEEE");

        em_network_node_t* bss_list = get_child(radio, "BSSList");
        if (!bss_list) { radios.push_back(r); continue; }

        for (int j = 0; j < static_cast<int>(bss_list->num_children); j++) {
            em_network_node_t* bss = bss_list->child[j];
            if (!bss) continue;

            std::string haul_type = get_tree_value(bss, "HaulType");
            std::string bss_ssid = get_tree_value(bss, "SSID");

            if (!haul_type.empty()) {
                TopoBSS tb;
                tb.bssid = get_tree_value(bss, "BSSID");
                tb.mld_addr = get_tree_value(bss, "MLDAddr");
                tb.haul_type = haul_type;
                tb.ssid = bss_ssid;
                tb.vap_mode = get_key_int_value(bss, "VapMode");
                tb.band = r.band;
                tb.vlan_id = get_key_int_value(bss, "VlanId");
                tb.ieee = r.ieee;
                r.bss.push_back(tb);
            }

            em_network_node_t* sta_list = get_child(bss, "STAList");
            if (!sta_list) continue;
            for (int k = 0; k < static_cast<int>(sta_list->num_children); k++) {
                em_network_node_t* sta = sta_list->child[k];
                if (!sta) continue;
                if (get_tree_value(sta, "Associated") != "true") continue;
                std::string sta_ssid = get_tree_value(sta, "SSID");
                if (sta_ssid.empty()) continue;
                if (haul_type == "Backhaul" && bss_ssid == sta_ssid) continue;

                TopoSTA t;
                t.sta_mac = get_tree_value(sta, "MACAddress");
                t.client_type = get_tree_value(sta, "ClientType");
                t.mld_addr = get_tree_value(sta, "MLDAddr");
                t.band = r.band;
                t.ssid = bss_ssid;
                r.stas.push_back(t);
            }
        }
        radios.push_back(r);
    }
    return radios;
}

// Direct port of Go's buildHaulTypes(): groups BSS entries by HaulType
// (Fronthaul/Backhaul/Iot), keyed on first-seen SSID/VlanId per type, with
// all matching BSS records collected underneath. Sorted by haul type name
// to match the Go version's deterministic `sort.Strings(sortedNames)`.
static std::vector<TopoHaulType> build_haul_types(const std::vector<RadioBssInfo>& radios) {
    std::vector<std::pair<std::string, TopoHaulType>> ordered; // preserves insertion for lookup

    for (auto& radio : radios) {
        for (auto& bss : radio.bss) {
            auto it = std::find_if(ordered.begin(), ordered.end(),
                                    [&](auto& p) { return p.first == bss.haul_type; });
            if (it == ordered.end()) {
                TopoHaulType ht;
                ht.name = bss.haul_type;
                ht.ssid = bss.ssid;
                ht.vlan_id = bss.vlan_id;
                ordered.push_back({bss.haul_type, ht});
                it = ordered.end() - 1;
            }
            it->second.bss_list.push_back(bss);
        }
    }

    std::sort(ordered.begin(), ordered.end(),
              [](auto& a, auto& b) { return a.first < b.first; });

    std::vector<TopoHaulType> result;
    for (auto& p : ordered) result.push_back(p.second);
    return result;
}

TopologyResult build_topology_from_device_tree(em_network_node_t* device_node) {
    TopologyResult result;
    int agent_count = 1, extender_count = 1;
    const float length = 230.0f;
    const double angle_step = 60.0;

    std::function<void(em_network_node_t*, float, float, double, int)> traverse =
        [&](em_network_node_t* node, float parent_x, float parent_y, double angle, int depth) {
        if (!node) return;

        std::string device_id = get_tree_value(node, "ID");
        em_network_node_t* backhaul_tree = get_child(node, "Backhaul");
        if (!backhaul_tree) return;

        em_network_node_t* radio_list_tree = (node->num_children > 2) ? node->child[2] : nullptr;
        auto radios_full = parse_radio_list_full(radio_list_tree);
        std::vector<TopoSTA> stas;
        for (auto& r : radios_full) stas.insert(stas.end(), r.stas.begin(), r.stas.end());
        auto haul_types = build_haul_types(radios_full);

        std::string backhaul_mac = get_tree_value(backhaul_tree, "MACAddress");
        std::string backhaul_media = get_tree_value(backhaul_tree, "MediaType");

        std::string device_name;
        if (depth == 0) {
            device_name = "Controller";
        } else if (backhaul_mac == "00:00:00:00:00:00" || backhaul_media == "Ethernet") {
            device_name = "Agent-" + std::to_string(agent_count++);
        } else {
            device_name = "Extender-" + std::to_string(extender_count++);
        }

        float x = 0, y = 0;
        if (depth != 0) {
            double theta = angle * (M_PI / 180.0);
            float current_length = (depth == 1) ? length * 0.7f : length;
            x = parent_x + current_length * static_cast<float>(std::cos(theta));
            y = parent_y + current_length * static_cast<float>(std::sin(theta));
        }

        TopoNode tn; tn.id = device_id; tn.name = device_name; tn.x = x; tn.y = y; tn.sta_list = stas; tn.haul_types = haul_types;
        result.nodes.push_back(tn);

        em_network_node_t* child_list = get_child(backhaul_tree, "Child");
        if (!child_list) return;

        double angle_spread = double(int(child_list->num_children) - 1) * angle_step;
        double start_angle = angle - angle_spread / 2;
        for (int i = 0; i < static_cast<int>(child_list->num_children); i++) {
            em_network_node_t* child = child_list->child[i];
            if (!child) continue;
            std::string child_id = get_tree_value(child, "ID");
            double child_angle = start_angle + double(i) * angle_step;

            int band = -1, channel = 0;
            em_network_node_t* child_radio_list = (child->num_children > 2) ? child->child[2] : nullptr;
            if (child_radio_list) {
                for (int r = 0; r < static_cast<int>(child_radio_list->num_children) && band == -1; r++) {
                    em_network_node_t* radio = child_radio_list->child[r];
                    if (!radio) continue;
                    em_network_node_t* bss_list = get_child(radio, "BSSList");
                    if (!bss_list) continue;
                    for (int b = 0; b < static_cast<int>(bss_list->num_children); b++) {
                        em_network_node_t* bss = bss_list->child[b];
                        if (get_key_int_value(bss, "VapMode") == 1 && get_tree_value(bss, "BSSID") != "00:00:00:00:00:00") {
                            band = get_key_int_value(radio, "Band");
                            channel = get_key_int_value(radio, "Channel");
                            break;
                        }
                    }
                }
            }

            TopoEdge te; te.from = device_id; te.to = child_id; te.band = band; te.channel = channel;
            result.edges.push_back(te);

            traverse(child, x, y, child_angle, depth + 1);
        }
    };

    traverse(device_node, 0, 0, 0, 0);
    return result;
}

// ===== Controller remote IP config =====

bool set_remote_addr_and_persist(const std::string& ip_str, unsigned int port) {
    if (port < 1 || port > 65535) return false;

    // Parse dotted-quad IPv4 and pack little-endian, matching the Go
    // version's binary.LittleEndian.Uint32(ip.To4()).
    unsigned int octets[4];
    if (std::sscanf(ip_str.c_str(), "%u.%u.%u.%u", &octets[0], &octets[1], &octets[2], &octets[3]) != 4)
        return false;
    for (auto o : octets) if (o > 255) return false;

    unsigned int ip_le = octets[0] | (octets[1] << 8) | (octets[2] << 16) | (octets[3] << 24);
    set_remote_addr(ip_le, port, true);

    // Persist to /nvram/remoteCtrl.json, matching remoteCtrl_Addr_path in main.go.
    std::ofstream out("/nvram/remoteCtrl.json");
    if (out.is_open()) {
        out << "{\n  \"ip\": \"" << ip_str << "\",\n  \"port\": \"" << port << "\"\n}\n";
    }
    return true;
}

std::pair<std::string, unsigned int> get_controller_remote_ip() {
    std::ifstream in("/nvram/remoteCtrl.json");
    if (!in.is_open()) return {"", 49153};

    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    cJSON* parsed = cJSON_Parse(content.c_str());
    if (!parsed) return {"", 49153};

    std::string ip;
    unsigned int port = 49153;
    if (cJSON* v = cJSON_GetObjectItem(parsed, "ip")) if (cJSON_IsString(v)) ip = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(parsed, "port")) if (cJSON_IsString(v)) port = static_cast<unsigned int>(std::atoi(v->valuestring));
    cJSON_Delete(parsed);
    return {ip, port};
}

std::pair<std::string, unsigned int> get_local_ip() {
    const unsigned int ctrl_port = 49153;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return {"", ctrl_port};

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(8888);
    inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);

    // connect() on a UDP socket sends no packets — it just lets the OS
    // routing table pick which local interface/address would be used to
    // reach that destination, which getsockname() then reveals. Same
    // trick as Go's net.Dial("udp", "8.8.8.8:8888") + LocalAddr().
    if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        close(sock);
        return {"", ctrl_port};
    }

    struct sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(sock, (struct sockaddr*)&local, &len) < 0) {
        close(sock);
        return {"", ctrl_port};
    }
    close(sock);

    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf));
    return {std::string(buf), ctrl_port};
}

void init_controller_connection() {
    auto [ip, port] = get_controller_remote_ip();
    if (ip.empty()) {
        auto [local_ip, local_port] = get_local_ip();
        ip = local_ip;
        port = local_port;
    }
    if (!ip.empty()) {
        set_remote_addr_and_persist(ip, port);
    }
}

} // namespace em
