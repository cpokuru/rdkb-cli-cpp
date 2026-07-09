#include "json_helpers.h"

std::string to_json_string(cJSON* obj) {
    char* raw = cJSON_PrintUnformatted(obj);
    std::string out = raw ? raw : "{}";
    if (raw) cJSON_free(raw);
    return out;
}

cJSON* point3d_to_json(const Point3D& p) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "x", p.x);
    cJSON_AddNumberToObject(o, "y", p.y);
    cJSON_AddNumberToObject(o, "z", p.z);
    return o;
}

cJSON* location_to_json(const Location& l) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "building", l.building);
    add_str(o, "floor", l.floor);
    add_str(o, "room", l.room);
    add_str(o, "description", l.description);
    cJSON_AddItemToObject(o, "position_3d", point3d_to_json(l.position_3d));
    return o;
}

cJSON* capability_to_json(const Capability& c) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddBoolToObject(o, "wifi7_support", c.wifi7_support);
    cJSON_AddNumberToObject(o, "max_mesh_links", c.max_mesh_links);
    add_str(o, "firmware", c.firmware);
    add_str(o, "serial_number", c.serial_number);
    cJSON* bands = cJSON_CreateArray();
    for (const auto& b : c.supported_bands) cJSON_AddItemToArray(bands, cJSON_CreateString(b.c_str()));
    cJSON_AddItemToObject(o, "supported_bands", bands);
    return o;
}

cJSON* device_metrics_to_json(const DeviceMetrics& m) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "cpu_usage_percent", m.cpu_usage_percent);
    cJSON_AddNumberToObject(o, "memory_usage_percent", m.memory_usage_percent);
    cJSON_AddNumberToObject(o, "temperature_celsius", m.temperature_celsius);
    cJSON_AddNumberToObject(o, "power_consumption_watts", m.power_consumption_watts);
    cJSON_AddNumberToObject(o, "tx_bytes", (double)m.tx_bytes);
    cJSON_AddNumberToObject(o, "rx_bytes", (double)m.rx_bytes);
    cJSON_AddNumberToObject(o, "tx_rate_mbps", m.tx_rate_mbps);
    cJSON_AddNumberToObject(o, "rx_rate_mbps", m.rx_rate_mbps);
    cJSON_AddNumberToObject(o, "tx_packets", (double)m.tx_packets);
    cJSON_AddNumberToObject(o, "rx_packets", (double)m.rx_packets);
    cJSON_AddNumberToObject(o, "error_rate_percent", m.error_rate_percent);
    cJSON_AddNumberToObject(o, "uptime_seconds", (double)m.uptime_seconds);
    cJSON_AddNumberToObject(o, "active_clients", m.active_clients);
    cJSON_AddNumberToObject(o, "channel_util_2_4ghz_percent", m.channel_util_2_4ghz_percent);
    cJSON_AddNumberToObject(o, "channel_util_5ghz_percent", m.channel_util_5ghz_percent);
    add_str(o, "last_updated", epoch_to_rfc3339(m.last_updated));
    return o;
}

cJSON* security_profile_to_json(const SecurityProfile& s) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "profile_name", s.profile_name);
    add_str(o, "auth_method", s.auth_method);
    add_str(o, "encryption_type", s.encryption_type);
    add_str(o, "security_level", s.security_level);
    return o;
}

cJSON* device_to_json(const Device& d) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "mac", d.mac);
    add_str(o, "role", d.role);
    add_str(o, "vendor", d.vendor);
    add_str(o, "model", d.model);
    add_str(o, "ip_address", d.ip_address);
    add_str(o, "status", d.status);
    add_str(o, "last_seen", epoch_to_rfc3339(d.last_seen));
    add_str(o, "uptime", d.uptime);
    cJSON_AddItemToObject(o, "capabilities", capability_to_json(d.capabilities));
    cJSON_AddItemToObject(o, "metrics", device_metrics_to_json(d.metrics));
    cJSON_AddItemToObject(o, "security_profile", security_profile_to_json(d.security_profile));
    cJSON_AddItemToObject(o, "location", location_to_json(d.location));
    return o;
}

cJSON* client_location_to_json(const ClientLocation& l) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddItemToObject(o, "estimated_position", point3d_to_json(l.estimated_position));
    add_str(o, "connected_ap", l.connected_ap);
    add_str(o, "last_update", epoch_to_rfc3339(l.last_update));
    cJSON_AddNumberToObject(o, "accuracy_meters", l.accuracy_meters);
    return o;
}

cJSON* client_metrics_to_json(const ClientMetrics& m) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "rssi_dbm", m.rssi_dbm);
    cJSON_AddNumberToObject(o, "snr_db", m.snr_db);
    cJSON_AddNumberToObject(o, "tx_rate_mbps", m.tx_rate_mbps);
    cJSON_AddNumberToObject(o, "rx_rate_mbps", m.rx_rate_mbps);
    cJSON_AddNumberToObject(o, "latency_ms", m.latency_ms);
    cJSON_AddNumberToObject(o, "data_usage_bytes", (double)m.data_usage_bytes);
    add_str(o, "last_updated", epoch_to_rfc3339(m.last_updated));
    cJSON_AddNumberToObject(o, "packet_loss_percent", m.packet_loss_percent);
    cJSON_AddNumberToObject(o, "retries", m.retries);
    cJSON_AddNumberToObject(o, "link_quality_percent", m.link_quality_percent);
    return o;
}

cJSON* client_to_json(const Client& c) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "mac", c.mac);
    add_str(o, "hostname", c.hostname);
    add_str(o, "ip_address", c.ip_address);
    add_str(o, "connected_ap_mac", c.connected_ap_mac);
    add_str(o, "connected_bssid", c.connected_bssid);
    add_str(o, "connection_time", epoch_to_rfc3339(c.connection_time));
    add_str(o, "device_type", c.device_type);
    add_str(o, "manufacturer", c.manufacturer);
    add_str(o, "last_activity", epoch_to_rfc3339(c.last_activity));
    cJSON_AddItemToObject(o, "client_metrics", client_metrics_to_json(c.client_metrics));
    cJSON_AddItemToObject(o, "location", client_location_to_json(c.location));
    return o;
}

cJSON* performance_alarm_to_json(const PerformanceAlarm& a) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", a.id);
    add_str(o, "severity", a.severity);
    add_str(o, "type", a.type);
    add_str(o, "message", a.message);
    cJSON_AddNumberToObject(o, "value", a.value);
    cJSON_AddNumberToObject(o, "threshold", a.threshold);
    add_str(o, "timestamp", epoch_to_rfc3339(a.timestamp));
    cJSON_AddBoolToObject(o, "acknowledged", a.acknowledged);
    return o;
}

cJSON* client_alarm_to_json(const ClientAlarm& a) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", a.id);
    add_str(o, "client_mac", a.client_mac);
    add_str(o, "severity", a.severity);
    add_str(o, "type", a.type);
    add_str(o, "message", a.message);
    cJSON_AddNumberToObject(o, "value", a.value);
    cJSON_AddNumberToObject(o, "threshold", a.threshold);
    add_str(o, "timestamp", epoch_to_rfc3339(a.timestamp));
    cJSON_AddBoolToObject(o, "acknowledged", a.acknowledged);
    return o;
}

cJSON* timeseries_metric_to_json(const TimeSeriesMetric& t) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "timestamp", epoch_to_rfc3339(t.timestamp));
    cJSON* values = cJSON_CreateObject();
    cJSON_AddNumberToObject(values, "cpu", t.cpu);
    cJSON_AddNumberToObject(values, "memory", t.memory);
    cJSON_AddNumberToObject(values, "temperature", t.temperature);
    cJSON_AddNumberToObject(values, "power", t.power);
    cJSON_AddNumberToObject(values, "tx_rate", t.tx_rate);
    cJSON_AddNumberToObject(values, "rx_rate", t.rx_rate);
    cJSON_AddNumberToObject(values, "error_rate", t.error_rate);
    cJSON_AddNumberToObject(values, "channel_util_24", t.channel_util_24);
    cJSON_AddNumberToObject(values, "channel_util_5", t.channel_util_5);
    cJSON_AddNumberToObject(values, "active_clients", t.active_clients);
    cJSON_AddItemToObject(o, "values", values);
    return o;
}

cJSON* system_config_to_json(const SystemConfig& s) {
    cJSON* o = cJSON_CreateObject();
    cJSON* ctrl = cJSON_CreateObject();
    cJSON_AddBoolToObject(ctrl, "auto_optimization", s.controller_settings.auto_optimization);
    cJSON_AddBoolToObject(ctrl, "channel_planning", s.controller_settings.channel_planning);
    cJSON_AddBoolToObject(ctrl, "power_management", s.controller_settings.power_management);
    cJSON_AddBoolToObject(ctrl, "firmware_management", s.controller_settings.firmware_management);
    cJSON_AddItemToObject(o, "controller_settings", ctrl);

    cJSON* sec = cJSON_CreateObject();
    cJSON_AddBoolToObject(sec, "intrusion_detection", s.security_settings.intrusion_detection);
    cJSON_AddBoolToObject(sec, "access_control", s.security_settings.access_control);
    cJSON_AddBoolToObject(sec, "threat_protection", s.security_settings.threat_protection);
    cJSON* allowed = cJSON_CreateArray();
    for (const auto& m : s.security_settings.allowed_macs) cJSON_AddItemToArray(allowed, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(sec, "allowed_macs", allowed);
    cJSON* blocked = cJSON_CreateArray();
    for (const auto& m : s.security_settings.blocked_macs) cJSON_AddItemToArray(blocked, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(sec, "blocked_macs", blocked);
    cJSON_AddItemToObject(o, "security_settings", sec);
    return o;
}

cJSON* mesh_topology_to_json(const MeshTopology& t) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "mesh_id", t.mesh_id);
    add_str(o, "controller_mac", t.controller_mac);
    cJSON_AddNumberToObject(o, "nodes", t.nodes);
    add_str(o, "protocol", t.protocol);
    add_str(o, "version", t.version);
    cJSON* perf = cJSON_CreateObject();
    cJSON_AddNumberToObject(perf, "average_throughput_mbps", t.performance.average_throughput_mbps);
    cJSON_AddNumberToObject(perf, "average_latency_ms", t.performance.average_latency_ms);
    cJSON_AddNumberToObject(perf, "total_clients", t.performance.total_clients);
    cJSON_AddItemToObject(o, "performance", perf);
    add_str(o, "last_updated", epoch_to_rfc3339(t.last_updated));
    return o;
}

std::string epoch_to_rfc3339(double epoch) {
    time_t sec = (time_t)epoch;
    struct tm tm_utc;
    gmtime_r(&sec, &tm_utc);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
    return std::string(buf);
}

cJSON* haul_config_to_json(const HaulConfig& h) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "HaulType", h.haul_type);
    add_str(o, "SSID", h.ssid);
    add_str(o, "PassPhrase", h.pass_phrase);
    cJSON_AddBoolToObject(o, "Enable", h.enabled);
    cJSON* bands = cJSON_CreateArray();
    for (const auto& b : h.bands) cJSON_AddItemToArray(bands, cJSON_CreateString(b.c_str()));
    cJSON_AddItemToObject(o, "Band", bands);
    add_str(o, "Security", h.security_type);
    cJSON_AddNumberToObject(o, "vlanId", h.vlan_id);
    return o;
}

HaulConfig haul_config_from_json(cJSON* obj) {
    HaulConfig h;
    if (cJSON* v = cJSON_GetObjectItem(obj, "HaulType")) if (cJSON_IsString(v)) h.haul_type = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "SSID")) if (cJSON_IsString(v)) h.ssid = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "PassPhrase")) if (cJSON_IsString(v)) h.pass_phrase = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "Enable")) h.enabled = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "Security")) if (cJSON_IsString(v)) h.security_type = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "vlanId")) if (cJSON_IsNumber(v)) h.vlan_id = v->valueint;
    if (cJSON* arr = cJSON_GetObjectItem(obj, "Band")) {
        int n = cJSON_GetArraySize(arr);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(arr, i);
            if (cJSON_IsString(item)) h.bands.push_back(item->valuestring);
        }
    }
    return h;
}
