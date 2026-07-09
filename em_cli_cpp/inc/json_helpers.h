#pragma once

#include <cjson/cJSON.h>
#include <string>
#include <memory>
#include "models.h"

// RAII wrapper so cJSON objects get freed even on early return / exception.
struct CJsonDeleter { void operator()(cJSON* p) const { if (p) cJSON_Delete(p); } };
using CJsonPtr = std::unique_ptr<cJSON, CJsonDeleter>;

inline void add_str(cJSON* obj, const char* key, const std::string& val) {
    cJSON_AddStringToObject(obj, key, val.c_str());
}

// ===== Serializers: struct -> cJSON* (caller owns / attaches returned object) =====

cJSON* point3d_to_json(const Point3D& p);
cJSON* location_to_json(const Location& l);
cJSON* capability_to_json(const Capability& c);
cJSON* device_metrics_to_json(const DeviceMetrics& m);
cJSON* security_profile_to_json(const SecurityProfile& s);
cJSON* device_to_json(const Device& d);

cJSON* client_location_to_json(const ClientLocation& l);
cJSON* client_metrics_to_json(const ClientMetrics& m);
cJSON* client_to_json(const Client& c);

cJSON* performance_alarm_to_json(const PerformanceAlarm& a);
cJSON* client_alarm_to_json(const ClientAlarm& a);
cJSON* timeseries_metric_to_json(const TimeSeriesMetric& t);

cJSON* system_config_to_json(const SystemConfig& s);
cJSON* mesh_topology_to_json(const MeshTopology& t);
cJSON* haul_config_to_json(const HaulConfig& h);

// Parses a HaulConfig from a cJSON object (inverse of haul_config_to_json),
// for POST /api/v1/wireless/profiles request bodies.
HaulConfig haul_config_from_json(cJSON* obj);

// ===== Extended serializers (radio/channel, coverage, topology, policy) =====

cJSON* radio_config_to_json(const RadioConfig& r);
RadioConfig radio_config_from_json(cJSON* obj);
cJSON* wifi_channel_config_to_json(const WifiChannelConfig& c);
cJSON* class_info_to_json(const ClassInfo& c);
std::vector<ChannelConfigEntry> channel_config_entries_from_json(cJSON* arr);

cJSON* advanced_wireless_settings_to_json(const AdvancedWirelessSettings& s);
AdvancedWirelessSettings advanced_wireless_settings_from_json(cJSON* obj);

cJSON* channel_scan_results_to_json(const ChannelScanResults& r);

cJSON* coverage_analysis_to_json(const CoverageAnalysis& a);
cJSON* weak_zone_to_json(const WeakZone& z);
cJSON* placement_suggestion_to_json(const PlacementSuggestion& s);
cJSON* signal_point_to_json(const SignalPoint& p);
CoverageRequest coverage_request_from_json(cJSON* obj);
OptimizationRequest optimization_request_from_json(cJSON* obj);

cJSON* floor_plan_to_json(const struct FloorPlan& f);

cJSON* topo_node_to_json(const TopoNode& n);
cJSON* topo_edge_to_json(const TopoEdge& e);

cJSON* wifi_policy_config_to_json(const WifiPolicyConfig& p);
WifiPolicyConfig wifi_policy_config_from_json(cJSON* obj);

cJSON* performance_alarm_history_to_json(const DevicePerformanceHistory& h);

// Serialize any of the above to a compact JSON string. Caller must free()
// the returned char* is handled internally (returns std::string, safe).
std::string to_json_string(cJSON* obj);
