#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "rbus_datamodel_bridge.h"
#include "rbus_bridge.h"
#include "handlers.h"
#include <cstring>
#include <cstdio>
#include <regex>
#include <fstream>
#include <sstream>

static bool validate_ssid(const std::string& ssid);
static bool validate_pass_phrase(const std::string& pass);

// ===== Static file serving =====
// The Go version's source hardcoded "/nvram/static/" for both
// router.PathPrefix("/static/") -> http.FileServer and "/" -> serveIndex.
// However, the actual deployed em_cli.service process runs with CWD
// /usr/ccsp/EasyMesh (confirmed via /proc/<pid>/cwd on the live gateway),
// and that's where the real static/ directory and JSON config files live
// — so that's the path used here instead of the literal source hardcode.
// If this ever needs to change again, it's this one constant.
static const char* STATIC_ROOT = "/usr/ccsp/EasyMesh/static/";


static const char* content_type_for_extension(const std::string& path) {
    auto ends_with = [&](const char* suffix) {
        size_t n = strlen(suffix);
        return path.size() >= n && path.compare(path.size() - n, n, suffix) == 0;
    };
    if (ends_with(".html")) return "text/html";
    if (ends_with(".js")) return "application/javascript";
    if (ends_with(".css")) return "text/css";
    if (ends_with(".json")) return "application/json";
    if (ends_with(".svg")) return "image/svg+xml";
    if (ends_with(".png")) return "image/png";
    if (ends_with(".jpg") || ends_with(".jpeg")) return "image/jpeg";
    if (ends_with(".ico")) return "image/x-icon";
    if (ends_with(".woff2")) return "font/woff2";
    if (ends_with(".woff")) return "font/woff";
    return "application/octet-stream";
}

static MHD_Result serve_file(struct MHD_Connection* connection, const std::string& fs_path) {
    std::ifstream in(fs_path, std::ios::binary);
    if (!in.is_open()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    return send_raw(connection, MHD_HTTP_OK, content_type_for_extension(fs_path), content);
}

// Blocks any path containing ".." so /static/../../etc/passwd-style
// requests can't escape the static root — Go's http.FileServer does this
// kind of normalization internally; this shim's transport (libwebsockets)
// doesn't, so it's done here explicitly.
static bool path_is_safe(const std::string& rel_path) {
    return rel_path.find("..") == std::string::npos;
}

MHD_Result send_json(struct MHD_Connection* connection, int status_code, const std::string& body) {
    connection->response_status = status_code;
    connection->response_content_type = "application/json";
    connection->response_body = body;
    return MHD_YES;
}

MHD_Result send_raw(struct MHD_Connection* connection, int status_code,
                     const std::string& content_type, const std::string& body,
                     const std::string& extra_header_name, const std::string& extra_header_value) {
    connection->response_status = status_code;
    connection->response_content_type = content_type;
    connection->response_body = body;
    connection->extra_header_name = extra_header_name;
    connection->extra_header_value = extra_header_value;
    return MHD_YES;
}

// ===== Route handlers (equivalent to the *Handler funcs in main.go) =====

static MHD_Result handle_get_devices(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.devices_mu);

    int online = 0;
    for (auto& d : state.devices) if (d.status == "Online") online++;

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& d : state.devices) cJSON_AddItemToArray(arr, device_to_json(d));
    cJSON_AddItemToObject(root.get(), "devices", arr);
    cJSON_AddNumberToObject(root.get(), "total", (double)state.devices.size());
    cJSON_AddNumberToObject(root.get(), "online", online);
    add_str(root.get(), "updated", epoch_to_rfc3339(now_epoch()));

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

static MHD_Result handle_get_device(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.devices_mu);
    for (auto& d : state.devices) {
        if (d.mac == mac) {
            CJsonPtr root(device_to_json(d));
            return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
        }
    }
    CJsonPtr err(cJSON_CreateObject());
    add_str(err.get(), "error", "Device not found");
    return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
}

static MHD_Result handle_get_clients(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.clients_mu);

    double now = now_epoch();
    int active = 0;
    for (auto& c : state.clients) if (now - c.last_activity < 300.0) active++;

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& c : state.clients) cJSON_AddItemToArray(arr, client_to_json(c));
    cJSON_AddItemToObject(root.get(), "clients", arr);
    cJSON_AddNumberToObject(root.get(), "total", (double)state.clients.size());
    cJSON_AddNumberToObject(root.get(), "active", active);
    add_str(root.get(), "updated", epoch_to_rfc3339(now));

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

static MHD_Result handle_get_client(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.clients_mu);
    for (auto& c : state.clients) {
        if (c.mac == mac) {
            CJsonPtr root(client_to_json(c));
            return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
        }
    }
    CJsonPtr err(cJSON_CreateObject());
    add_str(err.get(), "error", "Client not found");
    return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
}

static int calculate_health_score() {
    auto& state = AppState::instance();
    std::shared_lock lock(state.devices_mu);
    if (state.devices.empty()) return 0;
    int online = 0;
    for (auto& d : state.devices) if (d.status == "Online") online++;
    return (online * 100) / (int)state.devices.size();
}

static MHD_Result handle_system_status(struct MHD_Connection* connection) {
    auto& state = AppState::instance();

    int online_devices = 0;
    {
        std::shared_lock lock(state.devices_mu);
        for (auto& d : state.devices) if (d.status == "Online") online_devices++;
    }

    int active_clients = 0;
    double now = now_epoch();
    {
        std::shared_lock lock(state.clients_mu);
        for (auto& c : state.clients) if (now - c.last_activity < 300.0) active_clients++;
    }

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "controller", "running");
    add_str(root.get(), "version", "EasyMesh R6 v1.0.0");
    add_str(root.get(), "protocol", "IEEE 1905.1 + Multi-AP R6");
    cJSON_AddNumberToObject(root.get(), "mesh_nodes", (double)state.devices.size());
    cJSON_AddNumberToObject(root.get(), "online_nodes", online_devices);
    cJSON_AddNumberToObject(root.get(), "active_clients", active_clients);
    cJSON_AddNumberToObject(root.get(), "health_score", calculate_health_score());
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now));

    cJSON* features = cJSON_CreateObject();
    cJSON_AddBoolToObject(features, "wifi7_support", true);
    cJSON_AddBoolToObject(features, "coordinated_scan", true);
    cJSON_AddBoolToObject(features, "optimized_roaming", true);
    cJSON_AddBoolToObject(features, "traffic_separation", true);
    cJSON_AddBoolToObject(features, "advanced_security", true);
    cJSON_AddBoolToObject(features, "ai_optimization", true);
    cJSON_AddItemToObject(root.get(), "features", features);

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Wireless profiles (now rbus-backed, per the full-replace decision
// — see rbus_datamodel_bridge.cpp) =====

static MHD_Result handle_get_wireless_profiles(struct MHD_Connection* connection) {
    std::vector<HaulConfig> hauls = em_rbus::get_wireless_profiles();

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& h : hauls) cJSON_AddItemToArray(arr, haul_config_to_json(h));
    cJSON_AddItemToObject(root.get(), "haulConfig", arr);
    cJSON_AddNumberToObject(root.get(), "total", (double)hauls.size());

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

static MHD_Result handle_post_wireless_profiles(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed || !cJSON_IsArray(parsed.get())) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid request payload");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }

    // NOTE: Device.WiFi.DataElements.Network.SetSSID is a method
    // (confirmed via `discelements tr_181_service`), not a plain settable
    // property — so this goes through rbusMethod_Invoke, not rbus_set.
    // The param names below (SSID/PassPhrase/Band/HaulType) are inferred
    // from TR-181 naming conventions and the SSID.{i}.* table's own field
    // names, but the exact expected shape for SetSSID's *input* object
    // hasn't been confirmed against a real invocation yet — verify this
    // against a real device before relying on it, e.g. by trying it on one
    // profile first and checking the SSID actually changes.
    int n = cJSON_GetArraySize(parsed.get());
    for (int i = 0; i < n; i++) {
        HaulConfig haul = haul_config_from_json(cJSON_GetArrayItem(parsed.get(), i));

        if (!validate_ssid(haul.ssid)) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "Invalid SSID for " + haul.haul_type);
            return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
        }
        if (!haul.pass_phrase.empty() && !validate_pass_phrase(haul.pass_phrase)) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "Invalid PassPhrase for " + haul.haul_type);
            return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
        }

        CJsonPtr params(cJSON_CreateObject());
        add_str(params.get(), "SSID", haul.ssid);
        add_str(params.get(), "HaulType", haul.haul_type);
        if (!haul.pass_phrase.empty()) add_str(params.get(), "PassPhrase", haul.pass_phrase);
        cJSON_AddBoolToObject(params.get(), "Enable", haul.enabled);

        CJsonPtr result(em_rbus::invoke_method("Device.WiFi.DataElements.Network.SetSSID", params.get()));
        cJSON* error_field = cJSON_GetObjectItem(result.get(), "error");
        if (error_field) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "SetSSID failed for " + haul.haul_type + ": " +
                    (cJSON_IsString(error_field) ? error_field->valuestring : "unknown error"));
            return send_json(connection, MHD_HTTP_INTERNAL_SERVER_ERROR, to_json_string(err.get()));
        }
    }

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Profile updated successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// SSID/passphrase validation, matching validateSSID / validatePassPhrase in main.go.
static bool validate_ssid(const std::string& ssid) {
    if (ssid.empty() || ssid.size() > 32) return false;
    static const std::regex re("^[\\w\\-. ]+$");
    return std::regex_match(ssid, re);
}
static bool validate_pass_phrase(const std::string& pass) {
    return pass.size() >= 8 && pass.size() <= 63;
}

// ===== Route table =====
// Path params (e.g. {mac}) are matched with a light regex, same intent as
// gorilla/mux's api.HandleFunc("/devices/{mac}", ...).

MHD_Result route(struct MHD_Connection* connection, const std::string& m,
                  const std::string& path, const std::string& body) {
    if (m == "OPTIONS") {
        return send_json(connection, MHD_HTTP_OK, "{}");
    }

    std::smatch match;

    // Static dashboard assets — matches Go's "/" -> serveIndex and
    // "/static/" -> http.FileServer, both rooted at STATIC_ROOT.
    if (m == "GET" && path == "/") {
        return serve_file(connection, std::string(STATIC_ROOT) + "index.html");
    }
    if (m == "GET" && path.rfind("/static/", 0) == 0) {
        std::string rel = path.substr(strlen("/static/"));
        if (!path_is_safe(rel)) {
            CJsonPtr err(cJSON_CreateObject());
            add_str(err.get(), "error", "Invalid path");
            return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
        }
        return serve_file(connection, std::string(STATIC_ROOT) + rel);
    }

    if (m == "GET" && path == "/api/v1/devices") return handle_get_devices(connection);
    if (m == "GET" && path == "/api/v1/clients") return handle_get_clients(connection);
    if (m == "GET" && path == "/api/v1/system/status") return handle_system_status(connection);
    if (m == "GET" && path == "/api/v1/wireless/profiles") return handle_get_wireless_profiles(connection);
    if (m == "POST" && path == "/api/v1/wireless/profiles") return handle_post_wireless_profiles(connection, body);

    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/devices/([0-9A-Fa-f:]+)$")))
        return handle_get_device(connection, match[1].str());

    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/clients/([0-9A-Fa-f:]+)$")))
        return handle_get_client(connection, match[1].str());

    // ===== Device / client mutations =====
    if (m == "POST" && std::regex_match(path, match, std::regex("^/api/v1/devices/([0-9A-Fa-f:]+)/reboot$")))
        return handle_post_reboot_device(connection, match[1].str());
    if (m == "POST" && std::regex_match(path, match, std::regex("^/api/v1/clients/([0-9A-Fa-f:]+)/disconnect$")))
        return handle_post_disconnect_client(connection, match[1].str());
    if (m == "POST" && std::regex_match(path, match, std::regex("^/api/v1/clients/([0-9A-Fa-f:]+)/block$")))
        return handle_post_block_client(connection, match[1].str());
    if (m == "POST" && std::regex_match(path, match, std::regex("^/api/v1/clients/([0-9A-Fa-f:]+)/unblock$")))
        return handle_post_unblock_client(connection, match[1].str());

    // ===== Controller IP =====
    if (m == "GET" && path == "/api/v1/controllerIPConfig") return handle_get_controller_ip(connection);
    if (m == "POST" && path == "/api/v1/controllerIPConfig") return handle_post_controller_ip(connection, body);

    // ===== Wireless =====
    if (m == "GET" && path == "/api/v1/wireless/radios") return handle_get_radios(connection);
    if (m == "POST" && path == "/api/v1/wireless/radios") return handle_post_radios(connection, body);
    if (m == "PUT" && std::regex_match(path, match, std::regex("^/api/v1/wireless/radios/([^/]+)$")))
        return handle_put_radio(connection, match[1].str(), body);
    if (m == "GET" && path == "/api/v1/wireless/advanced") return handle_get_advanced(connection);
    if (m == "PUT" && path == "/api/v1/wireless/advanced") return handle_put_advanced(connection, body);
    if (m == "POST" && path == "/api/v1/wireless/scan") return handle_post_scan(connection, body);
    if (m == "GET" && path == "/api/v1/wireless/scan/results") return handle_get_scan_results(connection);
    if (m == "GET" && path == "/api/v1/wireless/config") return handle_get_wireless_config(connection);
    if (m == "PUT" && path == "/api/v1/wireless/config") return handle_put_wireless_config(connection, body);
    if ((m == "GET" || m == "POST") && path == "/api/v1/wifipolicy")
        return m == "GET" ? handle_get_wifi_policy(connection) : handle_post_wifi_policy(connection, body);
    if ((m == "GET" || m == "POST") && path == "/api/v1/wifireset")
        return m == "GET" ? handle_get_wifi_reset(connection) : handle_post_wifi_reset(connection, body);
    if (m == "POST" && path == "/api/v1/unassoc_sta_query") return handle_post_unassoc_sta_query(connection, body);

    // ===== Coverage / placement =====
    if (m == "GET" && path == "/api/v1/coverage/analysis") return handle_get_coverage_analysis(connection);
    if (m == "POST" && path == "/api/v1/coverage/analyze") return handle_post_coverage_analyze(connection, body);
    if (m == "POST" && path == "/api/v1/coverage/optimize") return handle_post_coverage_optimize(connection, body);
    if (m == "GET" && path == "/api/v1/coverage/floorplans") return handle_get_floorplans(connection);
    if (m == "POST" && path == "/api/v1/coverage/floorplans") return handle_post_floorplans(connection, body);
    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/coverage/floorplans/([^/]+)$")))
        return handle_get_floorplan(connection, match[1].str());
    if (m == "PUT" && std::regex_match(path, match, std::regex("^/api/v1/coverage/floorplans/([^/]+)$")))
        return handle_put_floorplan(connection, match[1].str(), body);
    if (m == "DELETE" && std::regex_match(path, match, std::regex("^/api/v1/coverage/floorplans/([^/]+)$")))
        return handle_delete_floorplan(connection, match[1].str());
    if (m == "GET" && path == "/api/v1/coverage/heatmap") return handle_get_heatmap(connection);
    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/coverage/heatmap/([^/]+)$")))
        return handle_get_band_heatmap(connection, match[1].str());
    if (m == "POST" && path == "/api/v1/coverage/simulate") return handle_post_simulate_placement(connection, body);
    if (m == "POST" && path == "/api/v1/coverage/placement/predict") return handle_post_predict_placement(connection, body);
    if (m == "GET" && path == "/api/v1/coverage/weakzones") return handle_get_weak_zones(connection);
    if (m == "GET" && path == "/api/v1/coverage/deadspots") return handle_get_dead_spots(connection);
    if (m == "GET" && path == "/api/v1/coverage/report") return handle_get_coverage_report(connection);
    if (m == "GET" && path == "/api/v1/coverage/report/pdf") return handle_get_coverage_report_pdf(connection);

    // ===== Topology =====
    if (m == "GET" && path == "/api/v1/topology") return handle_get_topology(connection);
    if (m == "POST" && path == "/api/v1/topology/optimize") return handle_post_topology_optimize(connection);

    // ===== Metrics / performance =====
    if (m == "GET" && path == "/api/v1/metrics/devices") return handle_get_device_metrics(connection);
    if (m == "GET" && path == "/api/v1/metrics/clients") return handle_get_client_metrics(connection);
    if (m == "GET" && path == "/api/v1/metrics/performance") return handle_get_performance_metrics(connection);
    if (m == "GET" && path == "/api/v1/metrics/interference") return handle_get_interference_analysis(connection);
    if (m == "GET" && path == "/api/v1/performance/devices") return handle_get_all_devices_performance(connection);
    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/performance/devices/([0-9A-Fa-f:]+)/clients$")))
        return handle_get_device_clients_performance(connection, match[1].str());
    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/performance/devices/([0-9A-Fa-f:]+)$")))
        return handle_get_device_performance(connection, match[1].str());
    if (m == "GET" && std::regex_match(path, match, std::regex("^/api/v1/performance/clients/([0-9A-Fa-f:]+)$")))
        return handle_get_client_performance(connection, match[1].str());
    if (m == "GET" && path == "/api/v1/performance/alarms") return handle_get_performance_alarms(connection);
    if (m == "POST" && std::regex_match(path, match, std::regex("^/api/v1/performance/alarms/([^/]+)/acknowledge$")))
        return handle_post_acknowledge_alarm(connection, match[1].str());

    // ===== Config / security / firmware / reports / logs =====
    if (m == "GET" && path == "/api/v1/config") return handle_get_config(connection);
    if (m == "PUT" && path == "/api/v1/config") return handle_put_config(connection, body);
    if (m == "GET" && path == "/api/v1/security/profiles") return handle_get_security_profiles(connection);
    if (m == "GET" && path == "/api/v1/security/threats") return handle_get_threat_analysis(connection);
    if (m == "GET" && path == "/api/v1/firmware/status") return handle_get_firmware_status(connection);
    if (m == "POST" && path == "/api/v1/firmware/update") return handle_post_firmware_update(connection);
    if (m == "GET" && path == "/api/v1/reports/usage") return handle_get_usage_report(connection);
    if (m == "GET" && path == "/api/v1/reports/performance") return handle_get_performance_report(connection);
    if (m == "GET" && path == "/api/v1/system/logs") return handle_get_system_logs(connection);

    // ===== rbus explorer (debug/inspection UI) =====
    if (m == "GET" && path == "/api/v1/rbus/status") return handle_get_rbus_status(connection);
    if (m == "POST" && path == "/api/v1/rbus/components") {
        CJsonPtr parsed(cJSON_Parse(body.c_str()));
        cJSON* p = parsed ? cJSON_GetObjectItem(parsed.get(), "path") : nullptr;
        return handle_get_rbus_components(connection, (p && cJSON_IsString(p)) ? p->valuestring : "");
    }
    if (m == "POST" && path == "/api/v1/rbus/elements") {
        CJsonPtr parsed(cJSON_Parse(body.c_str()));
        cJSON* c = parsed ? cJSON_GetObjectItem(parsed.get(), "component") : nullptr;
        return handle_get_rbus_elements(connection, (c && cJSON_IsString(c)) ? c->valuestring : "");
    }
    if (m == "POST" && path == "/api/v1/rbus/get") return handle_post_rbus_get(connection, body);
    if (m == "POST" && path == "/api/v1/rbus/invoke") return handle_post_rbus_method_invoke(connection, body);

    CJsonPtr err(cJSON_CreateObject());
    add_str(err.get(), "error", "Not found");
    return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
}
