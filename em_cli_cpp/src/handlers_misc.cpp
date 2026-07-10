#include "handlers.h"
#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "ws_server.h"
#include "rbus_datamodel_bridge.h"
#include <thread>
#include <chrono>
#include <algorithm>

// ===== System config =====

MHD_Result handle_get_config(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::lock_guard<std::mutex> lock(state.config_mu);
    CJsonPtr root(system_config_to_json(state.system_config));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_put_config(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    SystemConfig cfg;
    if (cJSON* ctrl = cJSON_GetObjectItem(parsed.get(), "controller_settings")) {
        if (cJSON* v = cJSON_GetObjectItem(ctrl, "auto_optimization")) cfg.controller_settings.auto_optimization = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(ctrl, "channel_planning")) cfg.controller_settings.channel_planning = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(ctrl, "power_management")) cfg.controller_settings.power_management = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(ctrl, "firmware_management")) cfg.controller_settings.firmware_management = cJSON_IsTrue(v);
    }
    if (cJSON* sec = cJSON_GetObjectItem(parsed.get(), "security_settings")) {
        if (cJSON* v = cJSON_GetObjectItem(sec, "intrusion_detection")) cfg.security_settings.intrusion_detection = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(sec, "access_control")) cfg.security_settings.access_control = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(sec, "threat_protection")) cfg.security_settings.threat_protection = cJSON_IsTrue(v);
    }

    auto& state = AppState::instance();
    { std::lock_guard<std::mutex> lock(state.config_mu); state.system_config = cfg; }

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "System configuration updated");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Security =====

MHD_Result handle_get_security_profiles(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();

    SecurityProfile p1{"Enterprise-Grade", "WPA3-SAE", "AES-256", "High"};
    SecurityProfile p2{"Consumer-Premium", "WPA3-SAE", "AES-256", "High"};
    cJSON_AddItemToArray(arr, security_profile_to_json(p1));
    cJSON_AddItemToArray(arr, security_profile_to_json(p2));

    cJSON_AddItemToObject(root.get(), "profiles", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_threat_analysis(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "threat_level", "low");
    cJSON_AddNumberToObject(root.get(), "active_threats", 0);
    cJSON_AddNumberToObject(root.get(), "blocked_attempts", 5);
    cJSON_AddNumberToObject(root.get(), "security_score", 98);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Firmware =====

MHD_Result handle_get_firmware_status(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "current_version", "6.2.1");
    add_str(root.get(), "latest_version", "6.2.2");
    cJSON_AddBoolToObject(root.get(), "update_available", true);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_firmware_update(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Firmware update initiated");
    add_str(root.get(), "estimated_time", "15 minutes");
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Reports =====

MHD_Result handle_get_usage_report(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "timeRange", "7d");
    add_str(root.get(), "totalData", "125.4 GB");
    add_str(root.get(), "avgThroughput", "847 Mbps");
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

static int calculate_health_score_misc() {
    auto& state = AppState::instance();
    std::shared_lock lock(state.devices_mu);
    if (state.devices.empty()) return 0;
    int online = 0;
    for (auto& d : state.devices) if (d.status == "Online") online++;
    return (online * 100) / (int)state.devices.size();
}

MHD_Result handle_get_performance_report(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "avgLatency", "3.2 ms");
    add_str(root.get(), "packetLoss", "0.08%");
    add_str(root.get(), "uptime", "99.95%");
    cJSON_AddNumberToObject(root.get(), "healthScore", calculate_health_score_misc());
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== System logs =====

MHD_Result handle_get_system_logs(struct MHD_Connection* connection) {
    double now = now_epoch();
    CJsonPtr root(cJSON_CreateObject());
    cJSON* logs = cJSON_CreateArray();

    auto add_log = [&](const char* level, const char* message, double offset_sec) {
        cJSON* l = cJSON_CreateObject();
        add_str(l, "level", level);
        add_str(l, "message", message);
        add_str(l, "timestamp", epoch_to_rfc3339(now - offset_sec));
        cJSON_AddItemToArray(logs, l);
    };
    add_log("info", "Client connected: MacBook Pro M3", 300);
    add_log("warning", "High channel utilization on 2.4GHz", 900);
    add_log("info", "Mesh optimization completed", 1800);

    cJSON_AddItemToObject(root.get(), "logs", logs);
    cJSON_AddNumberToObject(root.get(), "total", cJSON_GetArraySize(logs));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Device / client mutations (with WS broadcast, matching broadcastDeviceUpdate etc.) =====

MHD_Result handle_post_reboot_device(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    Device snapshot;
    bool found = false;
    {
        std::unique_lock lock(state.devices_mu);
        for (auto& d : state.devices) {
            if (d.mac == mac) {
                d.status = "Rebooting";
                d.last_seen = now_epoch();
                snapshot = d;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Device not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }

    CJsonPtr bcast(cJSON_CreateObject());
    add_str(bcast.get(), "type", "device_update");
    cJSON_AddItemToObject(bcast.get(), "device", device_to_json(snapshot));
    add_str(bcast.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    if (auto* ws = global_ws_server()) ws->broadcast(to_json_string(bcast.get()));

    // Matches the Go version's `go func() { sleep(30s); set back Online }()`
    std::thread([mac]() {
        std::this_thread::sleep_for(std::chrono::seconds(5)); // shortened from 30s for a responsive demo/test loop
        auto& state = AppState::instance();
        Device updated;
        bool ok = false;
        {
            std::unique_lock lock(state.devices_mu);
            for (auto& d : state.devices) {
                if (d.mac == mac) {
                    d.status = "Online";
                    d.last_seen = now_epoch();
                    updated = d;
                    ok = true;
                    break;
                }
            }
        }
        if (ok) {
            CJsonPtr bc(cJSON_CreateObject());
            add_str(bc.get(), "type", "device_update");
            cJSON_AddItemToObject(bc.get(), "device", device_to_json(updated));
            add_str(bc.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
            if (auto* ws = global_ws_server()) ws->broadcast(to_json_string(bc.get()));
        }
    }).detach();

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Device reboot initiated");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_disconnect_client(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    bool found = false;
    {
        std::unique_lock lock(state.clients_mu);
        auto it = std::find_if(state.clients.begin(), state.clients.end(),
                                [&](const Client& c) { return c.mac == mac; });
        if (it != state.clients.end()) { state.clients.erase(it); found = true; }
    }
    if (!found) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Client not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }

    CJsonPtr bcast(cJSON_CreateObject());
    add_str(bcast.get(), "type", "client_disconnected");
    add_str(bcast.get(), "mac", mac);
    add_str(bcast.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    if (auto* ws = global_ws_server()) ws->broadcast(to_json_string(bcast.get()));

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Client disconnected");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_block_client(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    { std::lock_guard<std::mutex> lock(state.config_mu); state.system_config.security_settings.blocked_macs.push_back(mac); }
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Client blocked");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_unblock_client(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    {
        std::lock_guard<std::mutex> lock(state.config_mu);
        auto& blocked = state.system_config.security_settings.blocked_macs;
        blocked.erase(std::remove(blocked.begin(), blocked.end(), mac), blocked.end());
    }
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Client unblocked");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Startup defaults (mirrors init() in main.go) =====

void init_wireless_and_coverage_defaults() {
    auto& state = AppState::instance();

    // initWirelessSettings()
    {
        std::unique_lock lock(state.wireless_mu);
        RadioConfig r24; r24.band = "2.4GHz"; r24.channel = 6; r24.channel_width = 40; r24.tx_power_dbm = 20;
        RadioConfig r5; r5.band = "5GHz"; r5.channel = 149; r5.channel_width = 80; r5.tx_power_dbm = 24; r5.dfs_enabled = true;
        RadioConfig r6; r6.band = "6GHz"; r6.channel = 37; r6.channel_width = 160; r6.tx_power_dbm = 30;
        state.radio_configs = { {"2.4GHz", r24}, {"5GHz", r5}, {"6GHz", r6} };
        state.advanced_settings.updated_at = now_epoch();
    }

    // initDefaultFloorPlans()
    {
        std::unique_lock lock(state.coverage_mu);
        FloorPlan f;
        f.id = "1st-floor"; f.name = "1st Floor"; f.url = "/nvram/static/floorplans/1st-floor.jpg";
        f.width_pixels = 1000; f.height_pixels = 600; f.scale_meters_per_pixel = 0.1;
        f.created_at = f.updated_at = now_epoch();
        state.floor_plans.push_back(f);
        FloorPlan f2 = f; f2.id = "default";
        state.floor_plans.push_back(f2);
    }

    // initPerformanceTracking()
    {
        std::vector<Device> devices_copy;
        { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }
        std::unique_lock lock(state.perf_mu);
        for (auto& d : devices_copy) {
            DevicePerformanceHistory h;
            h.device_mac = d.mac;
            h.device_name = d.model;
            h.last_updated = now_epoch();
            state.performance_history.push_back(h);
        }
    }
}

// Periodic background task matching updatePerformanceHistory() /
// checkDeviceAlarms() on the same 10s cadence as the metrics updater.
void start_performance_background_task() {
    std::thread([]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            auto& state = AppState::instance();

            std::vector<Device> devices_copy;
            { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

            std::unique_lock lock(state.perf_mu);
            for (auto& d : devices_copy) {
                auto* h = state.find_history(d.mac);
                if (!h) continue;

                TimeSeriesMetric m;
                m.timestamp = now_epoch();
                m.cpu = d.metrics.cpu_usage_percent;
                m.memory = d.metrics.memory_usage_percent;
                m.temperature = d.metrics.temperature_celsius;
                m.power = d.metrics.power_consumption_watts;
                h->metrics.push_back(m);
                if (h->metrics.size() > 100) h->metrics.erase(h->metrics.begin());

                if (d.metrics.cpu_usage_percent > 80) {
                    PerformanceAlarm a;
                    a.id = "cpu-" + d.mac + "-" + std::to_string(long(now_epoch()));
                    a.severity = "warning"; a.type = "cpu";
                    a.message = "High CPU usage: " + std::to_string(d.metrics.cpu_usage_percent) + "%";
                    a.value = d.metrics.cpu_usage_percent; a.threshold = 80; a.timestamp = now_epoch();
                    h->alarms.push_back(a);
                }
                if (d.metrics.memory_usage_percent > 85) {
                    PerformanceAlarm a;
                    a.id = "memory-" + d.mac + "-" + std::to_string(long(now_epoch()));
                    a.severity = "warning"; a.type = "memory";
                    a.message = "High memory usage: " + std::to_string(d.metrics.memory_usage_percent) + "%";
                    a.value = d.metrics.memory_usage_percent; a.threshold = 85; a.timestamp = now_epoch();
                    h->alarms.push_back(a);
                }
                if (d.metrics.temperature_celsius > 70) {
                    PerformanceAlarm a;
                    a.id = "temp-" + d.mac + "-" + std::to_string(long(now_epoch()));
                    a.severity = "critical"; a.type = "temperature";
                    a.message = "High temperature: " + std::to_string(d.metrics.temperature_celsius) + "C";
                    a.value = d.metrics.temperature_celsius; a.threshold = 70; a.timestamp = now_epoch();
                    h->alarms.push_back(a);
                }

                // Keep only alarms from the last hour, matching the Go version.
                double cutoff = now_epoch() - 3600;
                h->alarms.erase(std::remove_if(h->alarms.begin(), h->alarms.end(),
                                 [&](const PerformanceAlarm& a) { return a.timestamp < cutoff; }),
                                 h->alarms.end());

                h->last_updated = now_epoch();
            }
        }
    }).detach();
}

// ===== rbus-backed devices/clients refresh (full-replace-exec() path) =====

void start_rbus_refresh_task() {
    std::thread([]() {
        while (true) {
            auto devices = em_rbus::get_devices();
            auto clients = em_rbus::get_clients();

            auto& state = AppState::instance();
            if (!devices.empty()) {
                std::unique_lock lock(state.devices_mu);
                state.devices = devices;
            }
            if (!clients.empty()) {
                std::unique_lock lock(state.clients_mu);
                state.clients = clients;
            }
            // Sample data (sample_data.cpp) stays in place as whatever was
            // there before the first successful rbus fetch — if rbus isn't
            // connected yet at startup, the dashboard shows the fallback
            // fixtures rather than an empty screen, and picks up real data
            // as soon as this loop's first successful fetch lands.
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }).detach();
}
