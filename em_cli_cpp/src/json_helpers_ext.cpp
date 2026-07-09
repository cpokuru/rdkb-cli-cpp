#include "json_helpers.h"

// ===== Radio / channel config =====

cJSON* class_info_to_json(const ClassInfo& c) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "class", c.cls);
    cJSON* arr = cJSON_CreateArray();
    for (int ch : c.supported_channels) cJSON_AddItemToArray(arr, cJSON_CreateNumber(ch));
    cJSON_AddItemToObject(o, "supported_channels", arr);
    return o;
}

static cJSON* channel_config_entry_to_json(const ChannelConfigEntry& e) {
    cJSON* o = cJSON_CreateObject();
    if (!e.device_id.empty()) add_str(o, "device_id", e.device_id);
    add_str(o, "radio_id", e.radio_id);
    cJSON_AddNumberToObject(o, "radio_index", e.radio_index);
    cJSON_AddNumberToObject(o, "class", e.cls);
    cJSON* ch = cJSON_CreateArray();
    for (int c : e.channels) cJSON_AddItemToArray(ch, cJSON_CreateNumber(c));
    cJSON_AddItemToObject(o, "channels", ch);
    cJSON* pref = cJSON_CreateArray();
    for (int p : e.preferences) cJSON_AddItemToArray(pref, cJSON_CreateNumber(p));
    cJSON_AddItemToObject(o, "preferences", pref);
    return o;
}

cJSON* wifi_channel_config_to_json(const WifiChannelConfig& c) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "device_id", c.device_id);
    cJSON* sc = cJSON_CreateArray();
    for (auto& s : c.supported_class) cJSON_AddItemToArray(sc, class_info_to_json(s));
    cJSON_AddItemToObject(o, "supported_class", sc);
    cJSON* selc = cJSON_CreateArray();
    for (auto& s : c.selected_config) cJSON_AddItemToArray(selc, channel_config_entry_to_json(s));
    cJSON_AddItemToObject(o, "selected_config", selc);
    return o;
}

cJSON* radio_config_to_json(const RadioConfig& r) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "band", r.band);
    cJSON_AddBoolToObject(o, "enabled", r.enabled);
    cJSON_AddBoolToObject(o, "auto_channel", r.auto_channel);
    cJSON_AddNumberToObject(o, "channel", r.channel);
    cJSON_AddNumberToObject(o, "channel_width", r.channel_width);
    cJSON_AddBoolToObject(o, "tx_power_auto", r.tx_power_auto);
    cJSON_AddNumberToObject(o, "tx_power_dbm", r.tx_power_dbm);
    add_str(o, "country_code", r.country_code);
    cJSON_AddNumberToObject(o, "beacon_interval_ms", r.beacon_interval_ms);
    cJSON_AddNumberToObject(o, "dtim_period", r.dtim_period);
    cJSON_AddNumberToObject(o, "rts_threshold", r.rts_threshold);
    cJSON_AddNumberToObject(o, "fragmentation_threshold", r.fragmentation_threshold);
    cJSON_AddBoolToObject(o, "dfs_enabled", r.dfs_enabled);
    cJSON_AddBoolToObject(o, "psc_only", r.psc_only);
    cJSON* dl = cJSON_CreateArray();
    for (auto& d : r.device_list) cJSON_AddItemToArray(dl, wifi_channel_config_to_json(d));
    cJSON_AddItemToObject(o, "device_list", dl);
    return o;
}

RadioConfig radio_config_from_json(cJSON* obj) {
    RadioConfig r;
    if (cJSON* v = cJSON_GetObjectItem(obj, "band")) if (cJSON_IsString(v)) r.band = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "enabled")) r.enabled = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "auto_channel")) r.auto_channel = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "channel")) if (cJSON_IsNumber(v)) r.channel = v->valueint;
    if (cJSON* v = cJSON_GetObjectItem(obj, "channel_width")) if (cJSON_IsNumber(v)) r.channel_width = v->valueint;
    if (cJSON* v = cJSON_GetObjectItem(obj, "tx_power_dbm")) if (cJSON_IsNumber(v)) r.tx_power_dbm = v->valueint;
    if (cJSON* v = cJSON_GetObjectItem(obj, "country_code")) if (cJSON_IsString(v)) r.country_code = v->valuestring;
    return r;
}

std::vector<ChannelConfigEntry> channel_config_entries_from_json(cJSON* arr) {
    std::vector<ChannelConfigEntry> out;
    if (!arr || !cJSON_IsArray(arr)) return out;
    int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n; i++) {
        cJSON* item = cJSON_GetArrayItem(arr, i);
        ChannelConfigEntry e;
        if (cJSON* v = cJSON_GetObjectItem(item, "device_id")) if (cJSON_IsString(v)) e.device_id = v->valuestring;
        if (cJSON* v = cJSON_GetObjectItem(item, "radio_id")) if (cJSON_IsString(v)) e.radio_id = v->valuestring;
        if (cJSON* v = cJSON_GetObjectItem(item, "radio_index")) if (cJSON_IsNumber(v)) e.radio_index = v->valueint;
        if (cJSON* v = cJSON_GetObjectItem(item, "class")) if (cJSON_IsNumber(v)) e.cls = v->valueint;
        if (cJSON* ch = cJSON_GetObjectItem(item, "channels")) {
            int cn = cJSON_GetArraySize(ch);
            for (int k = 0; k < cn; k++) e.channels.push_back(cJSON_GetArrayItem(ch, k)->valueint);
        }
        if (cJSON* pr = cJSON_GetObjectItem(item, "preferences")) {
            int pn = cJSON_GetArraySize(pr);
            for (int k = 0; k < pn; k++) e.preferences.push_back(cJSON_GetArrayItem(pr, k)->valueint);
        }
        out.push_back(e);
    }
    return out;
}

// ===== Advanced wireless settings =====

cJSON* advanced_wireless_settings_to_json(const AdvancedWirelessSettings& s) {
    cJSON* o = cJSON_CreateObject();
    cJSON* bs = cJSON_CreateObject();
    cJSON_AddBoolToObject(bs, "enabled", s.band_steering.enabled);
    add_str(bs, "policy", s.band_steering.policy);
    cJSON_AddNumberToObject(bs, "rssi_threshold_2g4", s.band_steering.rssi_threshold_2g4);
    cJSON_AddNumberToObject(bs, "rssi_threshold_5g", s.band_steering.rssi_threshold_5g);
    cJSON_AddNumberToObject(bs, "rssi_threshold_6g", s.band_steering.rssi_threshold_6g);
    cJSON_AddNumberToObject(bs, "utilization_threshold_percent", s.band_steering.utilization_threshold_percent);
    cJSON_AddNumberToObject(bs, "block_time_seconds", s.band_steering.block_time_seconds);
    cJSON_AddBoolToObject(bs, "probe_response_suppression", s.band_steering.probe_response_suppression);
    cJSON_AddItemToObject(o, "band_steering", bs);

    cJSON* lb = cJSON_CreateObject();
    cJSON_AddBoolToObject(lb, "enabled", s.load_balancing.enabled);
    add_str(lb, "algorithm", s.load_balancing.algorithm);
    cJSON_AddNumberToObject(lb, "rebalance_interval_seconds", s.load_balancing.rebalance_interval_seconds);
    cJSON_AddNumberToObject(lb, "client_count_threshold", s.load_balancing.client_count_threshold);
    cJSON_AddNumberToObject(lb, "utilization_threshold_percent", s.load_balancing.utilization_threshold_percent);
    cJSON_AddNumberToObject(lb, "rssi_difference_threshold", s.load_balancing.rssi_difference_threshold);
    cJSON_AddItemToObject(o, "load_balancing", lb);

    cJSON_AddBoolToObject(o, "airtime_fairness", s.airtime_fairness);
    cJSON_AddBoolToObject(o, "fast_transition", s.fast_transition);
    cJSON_AddBoolToObject(o, "ofdma", s.ofdma);
    cJSON_AddBoolToObject(o, "mu_mimo", s.mu_mimo);
    cJSON_AddBoolToObject(o, "beamforming", s.beamforming);
    cJSON_AddBoolToObject(o, "twt", s.twt);
    cJSON_AddBoolToObject(o, "spatial_reuse", s.spatial_reuse);
    add_str(o, "updated_at", epoch_to_rfc3339(s.updated_at));
    return o;
}

AdvancedWirelessSettings advanced_wireless_settings_from_json(cJSON* obj) {
    AdvancedWirelessSettings s;
    if (cJSON* bs = cJSON_GetObjectItem(obj, "band_steering")) {
        if (cJSON* v = cJSON_GetObjectItem(bs, "enabled")) s.band_steering.enabled = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(bs, "policy")) if (cJSON_IsString(v)) s.band_steering.policy = v->valuestring;
        if (cJSON* v = cJSON_GetObjectItem(bs, "rssi_threshold_2g4")) s.band_steering.rssi_threshold_2g4 = v->valueint;
        if (cJSON* v = cJSON_GetObjectItem(bs, "rssi_threshold_5g")) s.band_steering.rssi_threshold_5g = v->valueint;
        if (cJSON* v = cJSON_GetObjectItem(bs, "rssi_threshold_6g")) s.band_steering.rssi_threshold_6g = v->valueint;
    }
    if (cJSON* lb = cJSON_GetObjectItem(obj, "load_balancing")) {
        if (cJSON* v = cJSON_GetObjectItem(lb, "enabled")) s.load_balancing.enabled = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(lb, "algorithm")) if (cJSON_IsString(v)) s.load_balancing.algorithm = v->valuestring;
    }
    if (cJSON* v = cJSON_GetObjectItem(obj, "airtime_fairness")) s.airtime_fairness = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "fast_transition")) s.fast_transition = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "ofdma")) s.ofdma = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "mu_mimo")) s.mu_mimo = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "beamforming")) s.beamforming = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "twt")) s.twt = cJSON_IsTrue(v);
    if (cJSON* v = cJSON_GetObjectItem(obj, "spatial_reuse")) s.spatial_reuse = cJSON_IsTrue(v);
    s.updated_at = now_epoch();
    return s;
}

// ===== Channel scan results =====

static cJSON* channel_info_to_json(const ChannelInfo& c) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "channel", c.channel);
    cJSON_AddNumberToObject(o, "frequency_mhz", c.frequency_mhz);
    cJSON_AddBoolToObject(o, "dfs_required", c.dfs_required);
    cJSON_AddNumberToObject(o, "max_tx_power_dbm", c.max_tx_power_dbm);
    add_str(o, "availability", c.availability);
    cJSON_AddNumberToObject(o, "utilization", c.utilization);
    cJSON_AddNumberToObject(o, "noise_floor_dbm", c.noise_floor_dbm);
    return o;
}

cJSON* channel_scan_results_to_json(const ChannelScanResults& r) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "scan_time", epoch_to_rfc3339(r.scan_time));
    cJSON_AddNumberToObject(o, "duration_seconds", r.duration_seconds);

    cJSON* results = cJSON_CreateObject();
    for (auto& [band, bandResult] : r.results) {
        cJSON* br = cJSON_CreateObject();
        add_str(br, "band", bandResult.band);
        cJSON* chans = cJSON_CreateArray();
        for (auto& c : bandResult.channels) cJSON_AddItemToArray(chans, channel_info_to_json(c));
        cJSON_AddItemToObject(br, "channels", chans);
        cJSON_AddNumberToObject(br, "interference_level", bandResult.interference_level);
        cJSON_AddNumberToObject(br, "average_noise_floor", bandResult.average_noise_floor);
        cJSON_AddItemToObject(results, band.c_str(), br);
    }
    cJSON_AddItemToObject(o, "results", results);

    cJSON* recs = cJSON_CreateObject();
    for (auto& [band, rec] : r.recommendations) {
        cJSON* rr = cJSON_CreateObject();
        cJSON_AddNumberToObject(rr, "recommended_channel", rec.recommended_channel);
        add_str(rr, "reason", rec.reason);
        cJSON_AddNumberToObject(rr, "expected_improvement_percent", rec.expected_improvement_percent);
        cJSON_AddItemToObject(recs, band.c_str(), rr);
    }
    cJSON_AddItemToObject(o, "recommendations", recs);
    return o;
}

// ===== Coverage =====

cJSON* signal_point_to_json(const SignalPoint& p) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "x", p.x);
    cJSON_AddNumberToObject(o, "y", p.y);
    cJSON_AddNumberToObject(o, "rssi", p.rssi);
    cJSON_AddNumberToObject(o, "snr", p.snr);
    add_str(o, "quality", p.quality);
    cJSON* sources = cJSON_CreateArray();
    for (auto& s : p.sources) cJSON_AddItemToArray(sources, cJSON_CreateString(s.c_str()));
    cJSON_AddItemToObject(o, "sources", sources);
    return o;
}

cJSON* weak_zone_to_json(const WeakZone& z) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", z.id);
    add_str(o, "points", z.points);
    cJSON_AddNumberToObject(o, "area_m2", z.area_m2);
    cJSON_AddNumberToObject(o, "average_rssi", z.average_rssi);
    add_str(o, "severity", z.severity);
    add_str(o, "reason", z.reason);
    cJSON* c = cJSON_CreateObject();
    cJSON_AddNumberToObject(c, "x", z.center.x);
    cJSON_AddNumberToObject(c, "y", z.center.y);
    cJSON_AddItemToObject(o, "center", c);
    return o;
}

cJSON* placement_suggestion_to_json(const PlacementSuggestion& s) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", s.id);
    cJSON_AddNumberToObject(o, "x", s.x);
    cJSON_AddNumberToObject(o, "y", s.y);
    cJSON_AddNumberToObject(o, "z", s.z);
    add_str(o, "device_type", s.device_type);
    cJSON_AddNumberToObject(o, "predicted_radius_m", s.predicted_radius_m);
    add_str(o, "predicted_quality", s.predicted_quality);
    add_str(o, "interference_risk", s.interference_risk);
    cJSON_AddNumberToObject(o, "coverage_improvement_percent", s.coverage_improvement_percent);
    cJSON_AddNumberToObject(o, "priority", s.priority);
    add_str(o, "reason", s.reason);
    return o;
}

cJSON* coverage_analysis_to_json(const CoverageAnalysis& a) {
    cJSON* o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "total_coverage", a.total_coverage);
    cJSON_AddNumberToObject(o, "excellent_coverage", a.excellent_coverage);
    cJSON_AddNumberToObject(o, "good_coverage", a.good_coverage);
    cJSON_AddNumberToObject(o, "fair_coverage", a.fair_coverage);
    cJSON_AddNumberToObject(o, "poor_coverage", a.poor_coverage);
    cJSON_AddNumberToObject(o, "weak_areas", a.weak_areas);
    cJSON_AddNumberToObject(o, "dead_zones", a.dead_zones);
    add_str(o, "interference_level", a.interference_level);
    cJSON* wz = cJSON_CreateArray();
    for (auto& z : a.weak_zones) cJSON_AddItemToArray(wz, weak_zone_to_json(z));
    cJSON_AddItemToObject(o, "weak_zones", wz);
    cJSON* ps = cJSON_CreateArray();
    for (auto& s : a.placement_suggestions) cJSON_AddItemToArray(ps, placement_suggestion_to_json(s));
    cJSON_AddItemToObject(o, "placement_suggestions", ps);
    if (!a.coverage_map.empty()) {
        cJSON* map = cJSON_CreateArray();
        for (auto& row : a.coverage_map) {
            cJSON* r = cJSON_CreateArray();
            for (auto& p : row) cJSON_AddItemToArray(r, signal_point_to_json(p));
            cJSON_AddItemToArray(map, r);
        }
        cJSON_AddItemToObject(o, "coverage_map", map);
    }
    add_str(o, "analyzed_at", epoch_to_rfc3339(a.analyzed_at));
    return o;
}

CoverageRequest coverage_request_from_json(cJSON* obj) {
    CoverageRequest r;
    if (!obj) return r;
    if (cJSON* v = cJSON_GetObjectItem(obj, "band")) if (cJSON_IsString(v)) r.band = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "threshold")) if (cJSON_IsNumber(v)) r.threshold = v->valueint;
    if (cJSON* v = cJSON_GetObjectItem(obj, "map_scale")) if (cJSON_IsNumber(v)) r.map_scale = v->valuedouble;
    if (cJSON* v = cJSON_GetObjectItem(obj, "resolution")) if (cJSON_IsNumber(v)) r.resolution = v->valueint;
    if (cJSON* v = cJSON_GetObjectItem(obj, "include_heatmap")) r.include_heatmap = cJSON_IsTrue(v);
    if (r.band.empty()) r.band = "2.4ghz";
    if (r.threshold == 0) r.threshold = -70;
    if (r.map_scale == 0) r.map_scale = 0.1;
    return r;
}

OptimizationRequest optimization_request_from_json(cJSON* obj) {
    OptimizationRequest r;
    if (!obj) return r;
    if (cJSON* v = cJSON_GetObjectItem(obj, "band")) if (cJSON_IsString(v)) r.band = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(obj, "coverage_target")) if (cJSON_IsNumber(v)) r.coverage_target = v->valuedouble;
    if (cJSON* v = cJSON_GetObjectItem(obj, "signal_threshold")) if (cJSON_IsNumber(v)) r.signal_threshold = v->valueint;
    if (r.band.empty()) r.band = "2.4ghz";
    if (r.coverage_target == 0) r.coverage_target = 95.0;
    if (r.signal_threshold == 0) r.signal_threshold = -70;
    return r;
}

cJSON* floor_plan_to_json(const FloorPlan& f) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", f.id);
    add_str(o, "name", f.name);
    add_str(o, "url", f.url);
    cJSON_AddNumberToObject(o, "width_pixels", f.width_pixels);
    cJSON_AddNumberToObject(o, "height_pixels", f.height_pixels);
    cJSON_AddNumberToObject(o, "scale_meters_per_pixel", f.scale_meters_per_pixel);
    add_str(o, "created_at", epoch_to_rfc3339(f.created_at));
    add_str(o, "updated_at", epoch_to_rfc3339(f.updated_at));
    return o;
}

// ===== Topology =====

cJSON* topo_node_to_json(const TopoNode& n) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", n.id);
    add_str(o, "name", n.name);
    cJSON_AddNumberToObject(o, "x", n.x);
    cJSON_AddNumberToObject(o, "y", n.y);
    cJSON* fixed = cJSON_CreateObject();
    cJSON_AddBoolToObject(fixed, "x", true);
    cJSON_AddBoolToObject(fixed, "y", true);
    cJSON_AddItemToObject(o, "fixed", fixed);
    cJSON* stas = cJSON_CreateArray();
    for (auto& s : n.sta_list) {
        cJSON* so = cJSON_CreateObject();
        add_str(so, "staMAC", s.sta_mac);
        add_str(so, "clientType", s.client_type);
        add_str(so, "MLDAddr", s.mld_addr);
        cJSON_AddNumberToObject(so, "band", s.band);
        add_str(so, "ssid", s.ssid);
        cJSON_AddItemToArray(stas, so);
    }
    cJSON_AddItemToObject(o, "STAList", stas);

    // Matches map.js's per-node haul-type overlay: haul.name, haul.ssid,
    // haul.VlanId, and haul.BSSList[].{BSSID,MLDAddr,vapMode,Band,IEEE}.
    cJSON* haul_types = cJSON_CreateArray();
    for (auto& h : n.haul_types) {
        cJSON* ho = cJSON_CreateObject();
        add_str(ho, "name", h.name);
        add_str(ho, "ssid", h.ssid);
        cJSON_AddNumberToObject(ho, "VlanId", h.vlan_id);
        cJSON* bss_arr = cJSON_CreateArray();
        for (auto& b : h.bss_list) {
            cJSON* bo = cJSON_CreateObject();
            add_str(bo, "BSSID", b.bssid);
            add_str(bo, "MLDAddr", b.mld_addr);
            cJSON_AddNumberToObject(bo, "vapMode", b.vap_mode);
            add_str(bo, "haulType", b.haul_type);
            cJSON_AddNumberToObject(bo, "VlanId", b.vlan_id);
            cJSON_AddNumberToObject(bo, "Band", b.band);
            add_str(bo, "IEEE", b.ieee);
            add_str(bo, "ssid", b.ssid);
            cJSON_AddItemToArray(bss_arr, bo);
        }
        cJSON_AddItemToObject(ho, "BSSList", bss_arr);
        cJSON_AddItemToArray(haul_types, ho);
    }
    cJSON_AddItemToObject(o, "haulTypes", haul_types);

    return o;
}

cJSON* topo_edge_to_json(const TopoEdge& e) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "from", e.from);
    add_str(o, "to", e.to);
    cJSON_AddNumberToObject(o, "band", e.band);
    cJSON_AddNumberToObject(o, "channel", e.channel);
    return o;
}

// ===== Wireless policy =====

cJSON* wifi_policy_config_to_json(const WifiPolicyConfig& p) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "id", p.id);

    cJSON* apm = cJSON_CreateObject();
    cJSON_AddNumberToObject(apm, "interval", p.ap_metric_reporting_policy.interval);
    add_str(apm, "managedClientMarker", p.ap_metric_reporting_policy.managed_client_marker);
    cJSON_AddItemToObject(o, "apMetricReportingPolicy", apm);

    cJSON* lsd = cJSON_CreateArray();
    for (auto& m : p.local_steering_disallowed) cJSON_AddItemToArray(lsd, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(o, "localSteeringDisallowed", lsd);

    cJSON* btm = cJSON_CreateArray();
    for (auto& m : p.btm_steering_disallowed) cJSON_AddItemToArray(btm, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(o, "btmSteeringDisallowed", btm);

    cJSON_AddNumberToObject(o, "reportIndependentChannelScans", p.report_independent_channel_scans);

    cJSON* dot1q = cJSON_CreateObject();
    cJSON_AddNumberToObject(dot1q, "primaryVLANID", p.default_8021q_settings_policy.primary_vlan_id);
    cJSON_AddNumberToObject(dot1q, "defaultPCP", p.default_8021q_settings_policy.default_pcp);
    cJSON_AddItemToObject(o, "default802_1Q_SettingsPolicy", dot1q);

    cJSON* unsucc = cJSON_CreateObject();
    cJSON_AddBoolToObject(unsucc, "reportUnsuccessAssoc", p.unsuccessful_assoc_policy.report_unsuccess_assoc);
    cJSON_AddNumberToObject(unsucc, "maxReportingRate", p.unsuccessful_assoc_policy.max_reporting_rate);
    cJSON_AddItemToObject(o, "unsuccessfulAssocPolicy", unsucc);

    cJSON* backhaul = cJSON_CreateArray();
    for (auto& b : p.backhaul_bss_config_policy) {
        cJSON* bo = cJSON_CreateObject();
        add_str(bo, "bssid", b.bssid);
        cJSON_AddBoolToObject(bo, "profile1bSTADisallowed", b.profile1_bsta_disallowed);
        cJSON_AddBoolToObject(bo, "profile2bSTADisallowed", b.profile2_bsta_disallowed);
        cJSON_AddItemToArray(backhaul, bo);
    }
    cJSON_AddItemToObject(o, "backhaulBssConfigPolicy", backhaul);

    cJSON* qos = cJSON_CreateObject();
    cJSON* mscs = cJSON_CreateArray();
    for (auto& m : p.qos_management_policy.mscs_disallowed_sta_list) cJSON_AddItemToArray(mscs, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(qos, "mscsDisallowedSTAList", mscs);
    cJSON* scs = cJSON_CreateArray();
    for (auto& m : p.qos_management_policy.scs_disallowed_sta_list) cJSON_AddItemToArray(scs, cJSON_CreateString(m.c_str()));
    cJSON_AddItemToObject(qos, "scsDisallowedSTAList", scs);
    cJSON_AddItemToObject(o, "qosManagementPolicy", qos);

    cJSON* rm = cJSON_CreateArray();
    for (auto& r : p.radio_specific_metrics_policy) {
        cJSON* ro = cJSON_CreateObject();
        add_str(ro, "id", r.id);
        cJSON_AddNumberToObject(ro, "starCPIThreshold", r.sta_rcpi_threshold);
        cJSON_AddNumberToObject(ro, "starCPIHysteresis", r.sta_rcpi_hysteresis);
        cJSON_AddNumberToObject(ro, "apUtilizationThreshold", r.ap_utilization_threshold);
        cJSON_AddNumberToObject(ro, "staTrafficStats", r.sta_traffic_stats);
        cJSON_AddNumberToObject(ro, "staLinkMetrics", r.sta_link_metrics);
        cJSON_AddNumberToObject(ro, "staStatus", r.sta_status);
        cJSON_AddItemToArray(rm, ro);
    }
    cJSON_AddItemToObject(o, "radioSpecificMetricsPolicy", rm);

    cJSON* rs = cJSON_CreateArray();
    for (auto& r : p.radio_steering_parameters_policy) {
        cJSON* ro = cJSON_CreateObject();
        add_str(ro, "id", r.id);
        cJSON_AddNumberToObject(ro, "steeringPolicy", r.steering_policy);
        cJSON_AddNumberToObject(ro, "utilizationThreshold", r.utilization_threshold);
        cJSON_AddNumberToObject(ro, "rcpiThreshold", r.rcpi_threshold);
        cJSON_AddItemToArray(rs, ro);
    }
    cJSON_AddItemToObject(o, "radioSteeringParametersPolicy", rs);

    return o;
}

WifiPolicyConfig wifi_policy_config_from_json(cJSON* obj) {
    WifiPolicyConfig p;
    if (cJSON* v = cJSON_GetObjectItem(obj, "id")) if (cJSON_IsString(v)) p.id = v->valuestring;
    if (cJSON* apm = cJSON_GetObjectItem(obj, "apMetricReportingPolicy")) {
        if (cJSON* v = cJSON_GetObjectItem(apm, "interval")) p.ap_metric_reporting_policy.interval = v->valueint;
        if (cJSON* v = cJSON_GetObjectItem(apm, "managedClientMarker")) if (cJSON_IsString(v)) p.ap_metric_reporting_policy.managed_client_marker = v->valuestring;
    }
    if (cJSON* v = cJSON_GetObjectItem(obj, "localSteeringDisallowed")) {
        int n = cJSON_GetArraySize(v);
        for (int i = 0; i < n; i++) p.local_steering_disallowed.push_back(cJSON_GetArrayItem(v, i)->valuestring);
    }
    if (cJSON* v = cJSON_GetObjectItem(obj, "btmSteeringDisallowed")) {
        int n = cJSON_GetArraySize(v);
        for (int i = 0; i < n; i++) p.btm_steering_disallowed.push_back(cJSON_GetArrayItem(v, i)->valuestring);
    }
    if (cJSON* v = cJSON_GetObjectItem(obj, "reportIndependentChannelScans")) p.report_independent_channel_scans = v->valueint;
    if (cJSON* dot1q = cJSON_GetObjectItem(obj, "default802_1Q_SettingsPolicy")) {
        if (cJSON* v = cJSON_GetObjectItem(dot1q, "primaryVLANID")) p.default_8021q_settings_policy.primary_vlan_id = v->valueint;
        if (cJSON* v = cJSON_GetObjectItem(dot1q, "defaultPCP")) p.default_8021q_settings_policy.default_pcp = v->valueint;
    }
    if (cJSON* unsucc = cJSON_GetObjectItem(obj, "unsuccessfulAssocPolicy")) {
        if (cJSON* v = cJSON_GetObjectItem(unsucc, "reportUnsuccessAssoc")) p.unsuccessful_assoc_policy.report_unsuccess_assoc = cJSON_IsTrue(v);
        if (cJSON* v = cJSON_GetObjectItem(unsucc, "maxReportingRate")) p.unsuccessful_assoc_policy.max_reporting_rate = v->valueint;
    }
    if (cJSON* backhaul = cJSON_GetObjectItem(obj, "backhaulBssConfigPolicy")) {
        int n = cJSON_GetArraySize(backhaul);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(backhaul, i);
            BackhaulBSSConfig b;
            if (cJSON* v = cJSON_GetObjectItem(item, "bssid")) if (cJSON_IsString(v)) b.bssid = v->valuestring;
            if (cJSON* v = cJSON_GetObjectItem(item, "profile1bSTADisallowed")) b.profile1_bsta_disallowed = cJSON_IsTrue(v);
            if (cJSON* v = cJSON_GetObjectItem(item, "profile2bSTADisallowed")) b.profile2_bsta_disallowed = cJSON_IsTrue(v);
            p.backhaul_bss_config_policy.push_back(b);
        }
    }
    if (cJSON* qos = cJSON_GetObjectItem(obj, "qosManagementPolicy")) {
        if (cJSON* v = cJSON_GetObjectItem(qos, "mscsDisallowedSTAList")) {
            int n = cJSON_GetArraySize(v);
            for (int i = 0; i < n; i++) p.qos_management_policy.mscs_disallowed_sta_list.push_back(cJSON_GetArrayItem(v, i)->valuestring);
        }
        if (cJSON* v = cJSON_GetObjectItem(qos, "scsDisallowedSTAList")) {
            int n = cJSON_GetArraySize(v);
            for (int i = 0; i < n; i++) p.qos_management_policy.scs_disallowed_sta_list.push_back(cJSON_GetArrayItem(v, i)->valuestring);
        }
    }
    if (cJSON* rm = cJSON_GetObjectItem(obj, "radioSpecificMetricsPolicy")) {
        int n = cJSON_GetArraySize(rm);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(rm, i);
            RadioSpecificMetrics r;
            if (cJSON* v = cJSON_GetObjectItem(item, "id")) if (cJSON_IsString(v)) r.id = v->valuestring;
            if (cJSON* v = cJSON_GetObjectItem(item, "starCPIThreshold")) r.sta_rcpi_threshold = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "starCPIHysteresis")) r.sta_rcpi_hysteresis = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "apUtilizationThreshold")) r.ap_utilization_threshold = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "staTrafficStats")) r.sta_traffic_stats = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "staLinkMetrics")) r.sta_link_metrics = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "staStatus")) r.sta_status = v->valueint;
            p.radio_specific_metrics_policy.push_back(r);
        }
    }
    if (cJSON* rs = cJSON_GetObjectItem(obj, "radioSteeringParametersPolicy")) {
        int n = cJSON_GetArraySize(rs);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(rs, i);
            RadioSteeringParameters r;
            if (cJSON* v = cJSON_GetObjectItem(item, "id")) if (cJSON_IsString(v)) r.id = v->valuestring;
            if (cJSON* v = cJSON_GetObjectItem(item, "steeringPolicy")) r.steering_policy = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "utilizationThreshold")) r.utilization_threshold = v->valueint;
            if (cJSON* v = cJSON_GetObjectItem(item, "rcpiThreshold")) r.rcpi_threshold = v->valueint;
            p.radio_steering_parameters_policy.push_back(r);
        }
    }
    return p;
}

// ===== Performance history =====

cJSON* performance_alarm_history_to_json(const DevicePerformanceHistory& h) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "device_mac", h.device_mac);
    add_str(o, "device_name", h.device_name);
    cJSON* metrics = cJSON_CreateArray();
    for (auto& m : h.metrics) cJSON_AddItemToArray(metrics, timeseries_metric_to_json(m));
    cJSON_AddItemToObject(o, "metrics", metrics);
    cJSON* alarms = cJSON_CreateArray();
    for (auto& a : h.alarms) cJSON_AddItemToArray(alarms, performance_alarm_to_json(a));
    cJSON_AddItemToObject(o, "alarms", alarms);
    add_str(o, "last_updated", epoch_to_rfc3339(h.last_updated));
    return o;
}
