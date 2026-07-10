#include "rbus_datamodel_bridge.h"
#include "rbus_bridge.h"
#include "json_helpers.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <functional>

namespace em_rbus {

namespace {

// Fetches one rbus path and returns its raw string value, or "" if the
// get failed / the path doesn't exist. Thin wrapper over the already-
// verified em_rbus::get_values() from rbus_bridge.cpp — reused rather
// than calling rbus_get directly, so there's exactly one place in this
// codebase that touches the raw rbus consumer API.
std::string get_single_value(const std::string& path) {
    CJsonPtr result(get_values({path}));
    cJSON* results = cJSON_GetObjectItem(result.get(), "results");
    if (!results || cJSON_GetArraySize(results) == 0) return "";
    cJSON* first = cJSON_GetArrayItem(results, 0);
    cJSON* value = cJSON_GetObjectItem(first, "value");
    if (!value || !cJSON_IsString(value)) return "";
    return value->valuestring;
}

// Runs a wildcard get (e.g. "Device.WiFi.DataElements.Network.SSID.*.SSID")
// and returns {index -> value} by parsing the resolved instance number out
// of each returned property's fully-qualified name. `prefix` is everything
// up to (and including) the wildcard segment's opening dot, matching the
// query pattern documented in rbus.h's "Option 4: Instance Wild card
// query" — one bus round-trip per queried leaf field, rather than a
// NumberOfEntries lookup followed by an N-iteration loop.
std::map<int, std::string> get_wildcard_column(const std::string& query, const std::string& prefix) {
    std::map<int, std::string> out;
    CJsonPtr result(get_values({query}));
    cJSON* results = cJSON_GetObjectItem(result.get(), "results");
    if (!results) return out;

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, results) {
        cJSON* name_node = cJSON_GetObjectItem(item, "name");
        cJSON* value_node = cJSON_GetObjectItem(item, "value");
        if (!name_node || !cJSON_IsString(name_node) || !value_node) continue;
        std::string name = name_node->valuestring;
        if (name.rfind(prefix, 0) != 0) continue;
        size_t idx_start = prefix.size();
        size_t idx_end = name.find('.', idx_start);
        if (idx_end == std::string::npos) continue;
        std::string idx_str = name.substr(idx_start, idx_end - idx_start);
        int idx = std::atoi(idx_str.c_str());
        out[idx] = cJSON_IsString(value_node) ? value_node->valuestring : "";
    }
    return out;
}

// ===== Topology JSON -> TopoNode/TopoEdge (cJSON version of what
// em_cli_bridge.cpp's build_topology_from_device_tree/build_haul_types did
// against the em_network_node_t tree — same algorithm, different tree API,
// verified against the same real topology3.json shape earlier) =====

std::string cj_str(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsString(v)) ? v->valuestring : "";
}
int cj_int(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return (v && cJSON_IsNumber(v)) ? v->valueint : 0;
}
bool cj_bool(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return v && cJSON_IsTrue(v);
}

struct RadioBssInfo {
    int band = 0;
    std::vector<TopoBSS> bss;
    std::vector<TopoSTA> stas;
};

std::vector<RadioBssInfo> parse_radio_list(cJSON* radio_list) {
    std::vector<RadioBssInfo> radios;
    if (!radio_list || !cJSON_IsArray(radio_list)) return radios;

    cJSON* radio = nullptr;
    cJSON_ArrayForEach(radio, radio_list) {
        RadioBssInfo r;
        r.band = cj_int(radio, "Band");

        cJSON* bss_list = cJSON_GetObjectItem(radio, "BSSList");
        if (bss_list && cJSON_IsArray(bss_list)) {
            cJSON* bss = nullptr;
            cJSON_ArrayForEach(bss, bss_list) {
                std::string haul_type = cj_str(bss, "HaulType");
                std::string bss_ssid = cj_str(bss, "SSID");

                if (!haul_type.empty()) {
                    TopoBSS tb;
                    tb.bssid = cj_str(bss, "BSSID");
                    tb.mld_addr = cj_str(bss, "MLDAddr");
                    tb.haul_type = haul_type;
                    tb.ssid = bss_ssid;
                    tb.vap_mode = cj_int(bss, "VapMode");
                    tb.band = r.band;
                    tb.vlan_id = cj_int(bss, "VlanID");
                    r.bss.push_back(tb);
                }

                cJSON* sta_list = cJSON_GetObjectItem(bss, "STAList");
                if (sta_list && cJSON_IsArray(sta_list)) {
                    cJSON* sta = nullptr;
                    cJSON_ArrayForEach(sta, sta_list) {
                        if (!cj_bool(sta, "Associated")) continue;
                        std::string sta_ssid = cj_str(sta, "SSID");
                        if (sta_ssid.empty()) continue;
                        if (haul_type == "Backhaul" && bss_ssid == sta_ssid) continue;

                        TopoSTA t;
                        t.sta_mac = cj_str(sta, "MACAddress");
                        t.client_type = cj_str(sta, "ClientType");
                        t.mld_addr = cj_str(sta, "MLDAddr");
                        t.band = r.band;
                        t.ssid = bss_ssid;
                        r.stas.push_back(t);
                    }
                }
            }
        }
        radios.push_back(r);
    }
    return radios;
}

std::vector<TopoHaulType> build_haul_types(const std::vector<RadioBssInfo>& radios) {
    std::vector<std::pair<std::string, TopoHaulType>> ordered;
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
    std::sort(ordered.begin(), ordered.end(), [](auto& a, auto& b) { return a.first < b.first; });
    std::vector<TopoHaulType> result;
    for (auto& p : ordered) result.push_back(p.second);
    return result;
}

// Recursive traversal matching em_cli_bridge.cpp's traverse lambda exactly
// (same depth-based naming: depth 0 = Controller, Ethernet-backhaul or
// zero-MAC = Agent, else Extender; same circular-layout angle math).
void traverse(cJSON* device, float parent_x, float parent_y, double angle, int depth,
              int& agent_count, int& extender_count,
              std::vector<TopoNode>& nodes, std::vector<TopoEdge>& edges) {
    if (!device) return;
    const float length = 230.0f;
    const double angle_step = 60.0;

    std::string device_id = cj_str(device, "ID");
    cJSON* backhaul = cJSON_GetObjectItem(device, "Backhaul");
    if (!backhaul) return;

    auto radios = parse_radio_list(cJSON_GetObjectItem(device, "RadioList"));
    std::vector<TopoSTA> stas;
    for (auto& r : radios) stas.insert(stas.end(), r.stas.begin(), r.stas.end());
    auto haul_types = build_haul_types(radios);

    std::string backhaul_mac = cj_str(backhaul, "MACAddress");
    std::string backhaul_media = cj_str(backhaul, "MediaType");

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

    TopoNode tn;
    tn.id = device_id; tn.name = device_name; tn.x = x; tn.y = y;
    tn.sta_list = stas; tn.haul_types = haul_types;
    nodes.push_back(tn);

    cJSON* child_list = cJSON_GetObjectItem(backhaul, "Child");
    if (!child_list || !cJSON_IsArray(child_list)) return;

    int child_count = cJSON_GetArraySize(child_list);
    double angle_spread = double(child_count - 1) * angle_step;
    double start_angle = angle - angle_spread / 2;

    int i = 0;
    cJSON* child = nullptr;
    cJSON_ArrayForEach(child, child_list) {
        std::string child_id = cj_str(child, "ID");
        double child_angle = start_angle + double(i) * angle_step;

        int band = -1, channel = 0;
        cJSON* child_radios = cJSON_GetObjectItem(child, "RadioList");
        if (child_radios && cJSON_IsArray(child_radios)) {
            cJSON* radio = nullptr;
            cJSON_ArrayForEach(radio, child_radios) {
                if (band != -1) break;
                cJSON* bss_list = cJSON_GetObjectItem(radio, "BSSList");
                if (!bss_list || !cJSON_IsArray(bss_list)) continue;
                cJSON* bss = nullptr;
                cJSON_ArrayForEach(bss, bss_list) {
                    if (cj_int(bss, "VapMode") == 1 && cj_str(bss, "BSSID") != "00:00:00:00:00:00") {
                        band = cj_int(radio, "Band");
                        channel = cj_int(radio, "Channel");
                        break;
                    }
                }
            }
        }

        TopoEdge te;
        te.from = device_id; te.to = child_id; te.band = band; te.channel = channel;
        edges.push_back(te);

        traverse(child, x, y, child_angle, depth + 1, agent_count, extender_count, nodes, edges);
        i++;
    }
}

} // namespace

TopologyResult get_topology() {
    TopologyResult result;
    std::string topo_json = get_single_value("Device.WiFi.DataElements.Network.Topology");
    if (topo_json.empty()) return result;

    cJSON* root = cJSON_Parse(topo_json.c_str());
    if (!root) return result;

    cJSON* device = cJSON_GetObjectItem(root, "Device");
    if (device) {
        int agent_count = 1, extender_count = 1;
        traverse(device, 0, 0, 0, 0, agent_count, extender_count, result.nodes, result.edges);
    }
    cJSON_Delete(root);
    return result;
}

std::vector<Client> get_clients() {
    std::vector<Client> clients;
    std::string topo_json = get_single_value("Device.WiFi.DataElements.Network.Topology");
    if (topo_json.empty()) return clients;

    cJSON* root = cJSON_Parse(topo_json.c_str());
    if (!root) return clients;

    // Flat walk of every Device -> RadioList -> BSSList -> STAList,
    // regardless of tree depth — clients don't need topology position,
    // just which AP/band/SSID they're associated to.
    std::function<void(cJSON*)> walk = [&](cJSON* device) {
        if (!device) return;
        std::string device_id = cj_str(device, "ID");
        cJSON* radio_list = cJSON_GetObjectItem(device, "RadioList");
        if (radio_list && cJSON_IsArray(radio_list)) {
            cJSON* radio = nullptr;
            cJSON_ArrayForEach(radio, radio_list) {
                cJSON* bss_list = cJSON_GetObjectItem(radio, "BSSList");
                if (!bss_list || !cJSON_IsArray(bss_list)) continue;
                cJSON* bss = nullptr;
                cJSON_ArrayForEach(bss, bss_list) {
                    cJSON* sta_list = cJSON_GetObjectItem(bss, "STAList");
                    if (!sta_list || !cJSON_IsArray(sta_list)) continue;
                    cJSON* sta = nullptr;
                    cJSON_ArrayForEach(sta, sta_list) {
                        if (!cj_bool(sta, "Associated")) continue;
                        Client c;
                        c.mac = cj_str(sta, "MACAddress");
                        c.hostname = cj_str(sta, "ClientType");
                        c.connected_ap_mac = device_id;
                        c.connected_bssid = cj_str(bss, "BSSID");
                        c.device_type = cj_str(sta, "ClientType");
                        c.last_activity = now_epoch();
                        c.client_metrics.last_updated = now_epoch();
                        clients.push_back(c);
                    }
                }
            }
        }
        cJSON* backhaul = cJSON_GetObjectItem(device, "Backhaul");
        if (backhaul) {
            cJSON* child_list = cJSON_GetObjectItem(backhaul, "Child");
            if (child_list && cJSON_IsArray(child_list)) {
                cJSON* child = nullptr;
                cJSON_ArrayForEach(child, child_list) walk(child);
            }
        }
    };
    walk(cJSON_GetObjectItem(root, "Device"));
    cJSON_Delete(root);
    return clients;
}

std::vector<Device> get_devices() {
    std::vector<Device> devices;

    // Metadata table (Manufacturer/SerialNumber/ManufacturerModel/
    // SoftwareVersion) — best-effort enrichment only. NOT used as the
    // primary key: on the real gateway, Device.{i}.ID in this table comes
    // back empty (confirmed via a real dmcli dump — every Device.{i}.ID
    // parameter had a blank value), which previously made every derived
    // Device end up with an empty .mac, breaking the topology cross-
    // reference AND the frontend's client-side "which AP is this client
    // on" lookup (both keyed on device MAC). Devices now come from
    // get_topology() instead, which has real IDs (proven working — the
    // topology diagram already renders correctly with real haul types and
    // client MACs). This metadata table is joined back in by table index
    // position only, on a best-effort basis.
    auto manufacturer = get_wildcard_column(
        "Device.WiFi.DataElements.Network.Device.*.Manufacturer",
        "Device.WiFi.DataElements.Network.Device.");
    auto serial = get_wildcard_column(
        "Device.WiFi.DataElements.Network.Device.*.SerialNumber",
        "Device.WiFi.DataElements.Network.Device.");
    auto model = get_wildcard_column(
        "Device.WiFi.DataElements.Network.Device.*.ManufacturerModel",
        "Device.WiFi.DataElements.Network.Device.");
    auto sw_version = get_wildcard_column(
        "Device.WiFi.DataElements.Network.Device.*.SoftwareVersion",
        "Device.WiFi.DataElements.Network.Device.");

    TopologyResult topo = get_topology();
    std::vector<Client> clients = get_clients(); // for per-device active_clients count

    int idx = 1; // TR-181 table indices are 1-based
    for (auto& node : topo.nodes) {
        Device d;
        d.mac = node.id;
        d.role = (node.name == "Controller") ? "Controller" : "Agent";
        d.status = "Online"; // presence in the live topology tree is the
                              // only "online" signal available over rbus
        d.last_seen = now_epoch();

        // Best-effort metadata join by position — real vendor/model data
        // if the table happens to align, empty (not "Unknown", just
        // genuinely blank) otherwise, which the frontend already handles
        // as a fallback display state.
        if (manufacturer.count(idx)) d.vendor = manufacturer[idx];
        if (model.count(idx)) d.model = model[idx];
        if (serial.count(idx)) d.capabilities.serial_number = serial[idx];
        if (sw_version.count(idx)) d.capabilities.firmware = sw_version[idx];

        int active_clients = 0;
        for (auto& c : clients) if (c.connected_ap_mac == node.id) active_clients++;
        d.metrics.active_clients = active_clients;
        d.metrics.last_updated = now_epoch();

        devices.push_back(d);
        idx++;
    }
    return devices;
}

std::vector<HaulConfig> get_wireless_profiles() {
    std::vector<HaulConfig> profiles;

    auto ssid_col = get_wildcard_column(
        "Device.WiFi.DataElements.Network.SSID.*.SSID",
        "Device.WiFi.DataElements.Network.SSID.");
    auto band_col = get_wildcard_column(
        "Device.WiFi.DataElements.Network.SSID.*.Band",
        "Device.WiFi.DataElements.Network.SSID.");
    auto enable_col = get_wildcard_column(
        "Device.WiFi.DataElements.Network.SSID.*.Enable",
        "Device.WiFi.DataElements.Network.SSID.");
    auto haul_col = get_wildcard_column(
        "Device.WiFi.DataElements.Network.SSID.*.HaulType",
        "Device.WiFi.DataElements.Network.SSID.");

    // Group by (HaulType, SSID) rather than emitting one row per table
    // index — the real table has one row per SSID-per-band (confirmed in
    // the dmcli dump: SSID.1 = private_ssid/2.4, SSID.2 = private_ssid/5),
    // but the wireless profiles UI card is per-SSID-name with a Band list,
    // matching how getConfiguredHauls() grouped by HaulType in the
    // original Go/exec() version.
    std::map<std::string, HaulConfig> grouped;
    for (auto& [idx, ssid] : ssid_col) {
        std::string haul = haul_col.count(idx) ? haul_col[idx] : "";
        std::string key = haul + "|" + ssid;
        auto& cfg = grouped[key];
        cfg.haul_type = haul;
        cfg.ssid = ssid;
        cfg.enabled = enable_col.count(idx) && enable_col[idx] == "true";
        std::string band = band_col.count(idx) ? band_col[idx] : "";
        if (!band.empty()) cfg.bands.push_back(band + "GHz");
        cfg.security_type = "WPA3-SAE"; // not exposed per-row in this table;
                                          // see header note on PassPhrase.
    }
    for (auto& [key, cfg] : grouped) profiles.push_back(cfg);
    return profiles;
}

} // namespace em_rbus
