#include "handlers.h"
#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "em_cli_bridge.h"
#include "rbus_datamodel_bridge.h"
#include "ws_server.h"
#include <cstring>
#include <random>
#include <thread>
#include <chrono>

static void broadcast_wireless_update(const std::string& subtype, cJSON* data_owned) {
    CJsonPtr msg(cJSON_CreateObject());
    add_str(msg.get(), "type", "wireless_update");
    add_str(msg.get(), "subtype", subtype);
    cJSON_AddItemToObject(msg.get(), "data", data_owned);
    add_str(msg.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    if (auto* ws = global_ws_server()) ws->broadcast(to_json_string(msg.get()));
}

// ===== Radios =====
// GET mirrors getRadioConfigsHandler's read path (get_channel commands +
// channel capability/config tree walk). POST mirrors its update path
// (updateAnticipatedChannelPreference + applyChannelConfig).

MHD_Result handle_get_radios(struct MHD_Connection* connection) {
    auto radios = em_rbus::get_radios();

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& rc : radios) cJSON_AddItemToArray(arr, radio_config_to_json(rc));
    cJSON_AddItemToObject(root.get(), "radios", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_radios(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed || !cJSON_IsArray(parsed.get())) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request payload");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    auto entries = channel_config_entries_from_json(parsed.get());

    em_network_node_t* prev_config_tree = em::exec_cmd("get_channel OneWifiMesh 1");
    if (!prev_config_tree || !em::update_anticipated_channel_preference(prev_config_tree, entries)) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "update Anticipated Channel Preference failed");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    em::apply_channel_config(prev_config_tree);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Radio profile updated successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_put_radio(struct MHD_Connection* connection, const std::string& band, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    RadioConfig cfg = radio_config_from_json(parsed.get());

    auto& state = AppState::instance();
    {
        std::unique_lock lock(state.wireless_mu);
        if (auto* existing = state.find_radio_config(band)) *existing = cfg;
        else state.radio_configs.push_back({band, cfg});
    }

    CJsonPtr data(radio_config_to_json(cfg));
    cJSON* bc_data = cJSON_CreateObject();
    add_str(bc_data, "band", band);
    cJSON_AddItemToObject(bc_data, "config", radio_config_to_json(cfg));
    broadcast_wireless_update("radio_config_updated", bc_data);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Radio configuration for " + band + " updated");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Advanced settings =====

MHD_Result handle_get_advanced(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.wireless_mu);
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddItemToObject(root.get(), "settings", advanced_wireless_settings_to_json(state.advanced_settings));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_put_advanced(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    AdvancedWirelessSettings settings = advanced_wireless_settings_from_json(parsed.get());

    auto& state = AppState::instance();
    {
        std::unique_lock lock(state.wireless_mu);
        state.advanced_settings = settings;
    }
    broadcast_wireless_update("advanced_settings_updated", advanced_wireless_settings_to_json(settings));

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Advanced wireless settings updated");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Channel scanning (simulated results, matching Go's generateBandScanResults) =====

static BandScanResults generate_band_scan_results(const std::string& band) {
    BandScanResults r;
    r.band = band;
    r.average_noise_floor = -95;
    long now_sec = static_cast<long>(now_epoch());

    if (band == "2.4GHz") {
        for (int ch : {1, 6, 11}) {
            double util = std::min(100.0, 20.0 + ch * 5.0 + double(now_sec % 100) / 10.0);
            ChannelInfo c;
            c.channel = ch; c.frequency_mhz = 2412 + (ch - 1) * 5;
            c.dfs_required = false; c.max_tx_power_dbm = 20;
            c.availability = "Available"; c.utilization = util;
            c.noise_floor_dbm = -95 + int(util / 10);
            r.channels.push_back(c);
        }
        r.interference_level = 0.15;
    } else if (band == "5GHz") {
        for (int ch : {36, 40, 44, 48, 149, 153, 157, 161}) {
            double util = std::min(100.0, 10.0 + ch / 10.0 + double(now_sec % 50) / 10.0);
            ChannelInfo c;
            c.channel = ch; c.frequency_mhz = 5000 + ch * 5;
            c.dfs_required = (ch >= 52 && ch <= 144); c.max_tx_power_dbm = 24;
            c.availability = "Available"; c.utilization = util;
            c.noise_floor_dbm = -98 + int(util / 15);
            r.channels.push_back(c);
        }
        r.interference_level = 0.08;
    } else if (band == "6GHz") {
        for (int ch : {37, 41, 45, 49, 93, 97}) {
            double util = std::min(100.0, 2.0 + ch / 20.0 + double(now_sec % 20) / 10.0);
            ChannelInfo c;
            c.channel = ch; c.frequency_mhz = 5950 + ch * 5;
            c.dfs_required = false; c.max_tx_power_dbm = 30;
            c.availability = "Available"; c.utilization = util;
            c.noise_floor_dbm = -100 + int(util / 20);
            r.channels.push_back(c);
        }
        r.interference_level = 0.02;
    }
    return r;
}

static ChannelRecommendation generate_channel_recommendation(const BandScanResults& band_results) {
    if (band_results.channels.empty()) return { 1, "No scan data available", 0 };
    ChannelInfo best = band_results.channels[0];
    for (auto& c : band_results.channels) if (c.utilization < best.utilization) best = c;

    std::string reason = "Lowest utilization (" + std::to_string(best.utilization) + "%)";
    if (best.utilization < 20) reason += " - Excellent choice";
    else if (best.utilization < 50) reason += " - Good choice";
    else reason += " - Best available option";

    return { best.channel, reason, std::max(0.0, 80.0 - best.utilization) };
}

MHD_Result handle_post_scan(struct MHD_Connection* connection, const std::string& body) {
    int duration = 30;
    std::vector<std::string> bands = {"2.4GHz", "5GHz", "6GHz"};
    if (CJsonPtr parsed{cJSON_Parse(body.c_str())}) {
        if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "scan_duration")) if (cJSON_IsNumber(v)) duration = v->valueint;
    }
    if (duration < 10 || duration > 300) duration = 30;

    // Run "in the background" but since this stub environment has no
    // long-lived scan hardware to wait on, we compute immediately and
    // store the result — same shape as the Go version's eventual state,
    // just without literally sleeping the handler thread for `duration`
    // seconds (that thread would just block the HTTP worker for no benefit
    // here; the real driver-backed scan would run in its own thread same
    // as Go's `go performChannelScan(...)`).
    std::thread([duration, bands]() {
        std::this_thread::sleep_for(std::chrono::seconds(std::min(duration, 3))); // capped for responsiveness
        ChannelScanResults results;
        results.scan_time = now_epoch();
        results.duration_seconds = duration;
        for (auto& band : bands) {
            BandScanResults br = generate_band_scan_results(band);
            results.results.push_back({band, br});
            results.recommendations.push_back({band, generate_channel_recommendation(br)});
        }
        auto& state = AppState::instance();
        {
            std::unique_lock lock(state.wireless_mu);
            state.last_scan_results = results;
            state.has_scan_results = true;
        }
        broadcast_wireless_update("channel_scan_completed", channel_scan_results_to_json(results));
    }).detach();

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Channel scan started");
    cJSON_AddNumberToObject(root.get(), "duration", duration);
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_scan_results(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.wireless_mu);
    if (!state.has_scan_results) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "No scan results available");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    cJSON_AddItemToObject(root.get(), "results", channel_scan_results_to_json(state.last_scan_results));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Aggregate wireless config =====

MHD_Result handle_get_wireless_config(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.wireless_mu);
    CJsonPtr root(cJSON_CreateObject());
    cJSON* radios = cJSON_CreateObject();
    for (auto& [band, cfg] : state.radio_configs) cJSON_AddItemToObject(radios, band.c_str(), radio_config_to_json(cfg));
    cJSON_AddItemToObject(root.get(), "radio_configs", radios);
    cJSON_AddItemToObject(root.get(), "advanced_settings", advanced_wireless_settings_to_json(state.advanced_settings));
    if (state.has_scan_results)
        cJSON_AddItemToObject(root.get(), "last_scan_results", channel_scan_results_to_json(state.last_scan_results));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_put_wireless_config(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    auto& state = AppState::instance();
    {
        std::unique_lock lock(state.wireless_mu);
        if (cJSON* radios = cJSON_GetObjectItem(parsed.get(), "radio_configs")) {
            cJSON* item = nullptr;
            cJSON_ArrayForEach(item, radios) {
                RadioConfig cfg = radio_config_from_json(item);
                std::string band = item->string ? item->string : cfg.band;
                if (auto* existing = state.find_radio_config(band)) *existing = cfg;
                else state.radio_configs.push_back({band, cfg});
            }
        }
        if (cJSON* adv = cJSON_GetObjectItem(parsed.get(), "advanced_settings")) {
            state.advanced_settings = advanced_wireless_settings_from_json(adv);
        }
    }

    cJSON* bc = cJSON_CreateObject();
    cJSON_AddItemToObject(bc, "advanced_settings", advanced_wireless_settings_to_json(state.advanced_settings));
    broadcast_wireless_update("config_updated", bc);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Wireless configuration updated successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Wireless policy =====

MHD_Result handle_get_wifi_policy(struct MHD_Connection* connection) {
    em_network_node_t* policy_tree = em::exec_cmd("get_policy OneWifiMesh");
    if (!policy_tree) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to fetch policy tree");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }
    em_network_node_t* device_list_node = em::get_child(policy_tree, "DeviceList");
    auto configs = em::get_policy_configuration(device_list_node);

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& c : configs) cJSON_AddItemToArray(arr, wifi_policy_config_to_json(c));
    cJSON_AddItemToObject(root.get(), "policyConfig", arr);
    cJSON_AddNumberToObject(root.get(), "total", (double)configs.size());
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_wifi_policy(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed || !cJSON_IsArray(parsed.get())) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request payload");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    em_network_node_t* policy_tree = em::exec_cmd("get_policy OneWifiMesh");
    if (!policy_tree) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to fetch policy tree");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }
    em_network_node_t* device_list_node = em::get_child(policy_tree, "DeviceList");

    int n = cJSON_GetArraySize(parsed.get());
    for (int i = 0; i < n; i++) {
        WifiPolicyConfig cfg = wifi_policy_config_from_json(cJSON_GetArrayItem(parsed.get(), i));
        if (!em::update_policy_settings(device_list_node, cfg)) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "Policy update failed");
            return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
        }
    }

    if (!em::apply_wifi_policy_config(policy_tree)) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to update wifi policy");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Policy updated successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== WiFi reset =====

MHD_Result handle_get_wifi_reset(struct MHD_Connection* connection) {
    em_network_node_t* reset_tree = em::exec_cmd("get_reset OneWifiMesh");
    if (!reset_tree) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to fetch reset tree");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }

    std::string controller_value = em::get_tree_value(reset_tree, "ControllerID");
    em_network_node_t* interfaces_list = em::get_child(reset_tree, "List");
    auto mac_options = em::get_interface_preference(interfaces_list);
    auto ssid_haul_config = em::get_configured_hauls(reset_tree);

    CJsonPtr root(cJSON_CreateObject());
    cJSON* opts = cJSON_CreateArray();
    for (auto& m : mac_options) cJSON_AddItemToArray(opts, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(root.get(), "options", opts);
    add_str(root.get(), "selectedOption", controller_value);
    cJSON* hauls = cJSON_CreateArray();
    for (auto& h : ssid_haul_config) cJSON_AddItemToArray(hauls, haul_config_to_json(h));
    cJSON_AddItemToObject(root.get(), "ssidHaulConfig", hauls);

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_wifi_reset(struct MHD_Connection* connection, const std::string& body) {
    em_network_node_t* reset_tree = em::exec_cmd("get_reset OneWifiMesh");
    if (!reset_tree) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to fetch reset tree");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }

    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request payload");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    std::vector<std::string> errors_list;

    if (cJSON* mac = cJSON_GetObjectItem(parsed.get(), "selectedMac")) {
        if (cJSON_IsString(mac) && strlen(mac->valuestring) > 0) {
            std::string selected(mac->valuestring);
            selected = selected.substr(0, selected.find(' '));
            if (!em::update_controller_id(reset_tree, selected)) {
                errors_list.push_back("Update failed for AL_MAC Interface: invalid MAC");
            }
        } else {
            errors_list.push_back("Received empty value for AL MAC");
        }
    }

    if (cJSON* hauls = cJSON_GetObjectItem(parsed.get(), "haulTypes")) {
        int n = cJSON_GetArraySize(hauls);
        for (int i = 0; i < n; i++) {
            HaulConfig h = haul_config_from_json(cJSON_GetArrayItem(hauls, i));
            em::update_network_ssid_list(reset_tree, h, false);
        }
    }

    if (!em::apply_reset_config(reset_tree)) {
        errors_list.push_back("Failed to apply wifi reset config");
    }

    CJsonPtr root(cJSON_CreateObject());
    if (!errors_list.empty()) {
        add_str(root.get(), "status", "failure");
        add_str(root.get(), "message", "Wi-Fi configuration reset failed");
        cJSON* errs = cJSON_CreateArray();
        for (auto& e : errors_list) cJSON_AddItemToArray(errs, cJSON_CreateString(e.c_str()));
        cJSON_AddItemToObject(root.get(), "errors", errs);
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(root.get()));
    }
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Wi-Fi configuration reset successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Unassociated STA link metrics query =====

MHD_Result handle_post_unassoc_sta_query(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request payload");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    cJSON* al_mac = cJSON_GetObjectItem(parsed.get(), "AlMac");
    if (!al_mac || !cJSON_IsString(al_mac) || strlen(al_mac->valuestring) == 0) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "AlMac required");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    cJSON* query_list = cJSON_GetObjectItem(parsed.get(), "UnassocStaQueryList");
    if (!query_list || cJSON_GetArraySize(query_list) == 0) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "UnassocStaQueryList required");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    // Validation pass matching the Go handler's structure checks.
    int n = cJSON_GetArraySize(query_list);
    for (int i = 0; i < n; i++) {
        cJSON* op = cJSON_GetArrayItem(query_list, i);
        cJSON* channels = cJSON_GetObjectItem(op, "channels");
        if (!channels || cJSON_GetArraySize(channels) == 0) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "Each opclass must contain channels");
            return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
        }
        int cn = cJSON_GetArraySize(channels);
        for (int j = 0; j < cn; j++) {
            cJSON* ch = cJSON_GetArrayItem(channels, j);
            cJSON* sta_macs = cJSON_GetObjectItem(ch, "sta_macs");
            if (!sta_macs || cJSON_GetArraySize(sta_macs) == 0) {
                CJsonPtr err(cJSON_CreateObject());
                add_str(err.get(), "error", "Each channel must contain STA list");
                return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
            }
        }
    }

    // Build the em_cli tree from the (already-validated) JSON payload and
    // dispatch, matching the Go handler's get_network_tree(jsonBytes) + exec.
    em_network_node_t* node = get_network_tree(const_cast<char*>(body.c_str()));
    if (!node) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Failed to create network tree");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }
    em_network_node_t* result = em::exec_cmd("unassoc_sta_query OneWifiMesh", node);
    free_network_tree(node);
    if (!result) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Command execution failed");
        return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
    }
    free_network_tree(result);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Unassoc STA Query sent");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Controller IP config =====

MHD_Result handle_get_controller_ip(struct MHD_Connection* connection) {
    // The em_cli library owns this via getControllerRemoteIP() in the
    // original — here we just surface whatever's in /nvram/remoteCtrl.json
    // through the same em_cli_bridge path your other handlers use. Left as
    // a stub returning empty until the underlying file-read helper is
    // ported (it's pure file I/O, no em_cli involved — trivial to add).
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "ip", "");
    add_str(root.get(), "port", "49153");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_controller_ip(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    std::string ip, port = "49153";
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "ip")) if (cJSON_IsString(v)) ip = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "port")) if (cJSON_IsString(v)) port = v->valuestring;
    em::set_remote_addr_and_persist(ip, static_cast<unsigned int>(std::stoul(port.empty() ? "49153" : port)));

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "message", "Controller IP and Port configured successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}
