/*
  If not stated otherwise in this file or this component's LICENSE file the
  following copyright and licenses apply:

  Copyright 2023 RDK Management

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>

// ===== TIME HELPERS =====
// Mirrors Go's time.Time usage: we store epoch seconds (double for sub-second)
// and format as RFC3339 for JSON output, matching the original Go behavior.
inline double now_epoch() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

std::string epoch_to_rfc3339(double epoch);

// ===== CORE DEVICE / CLIENT MODELS =====

struct Capability {
    bool wifi7_support = false;
    int max_mesh_links = 0;
    std::string firmware;
    std::string serial_number;
    std::vector<std::string> supported_bands;
};

struct DeviceMetrics {
    double cpu_usage_percent = 0;
    double memory_usage_percent = 0;
    double temperature_celsius = 0;
    double power_consumption_watts = 0;
    uint64_t tx_bytes = 0;
    uint64_t rx_bytes = 0;
    double tx_rate_mbps = 0;
    double rx_rate_mbps = 0;
    uint64_t tx_packets = 0;
    uint64_t rx_packets = 0;
    double error_rate_percent = 0;
    int64_t uptime_seconds = 0;
    int active_clients = 0;
    double channel_util_2_4ghz_percent = 0;
    double channel_util_5ghz_percent = 0;
    double last_updated = 0;
};

struct SecurityProfile {
    std::string profile_name;
    std::string auth_method;
    std::string encryption_type;
    std::string security_level;
};

struct Point3D {
    double x = 0, y = 0, z = 0;
};

struct Location {
    std::string building;
    std::string floor;
    std::string room;
    std::string description;
    Point3D position_3d;
};

struct Device {
    std::string mac;
    std::string role;
    std::string vendor;
    std::string model;
    std::string ip_address;
    std::string status;
    double last_seen = 0;
    std::string uptime;
    Capability capabilities;
    DeviceMetrics metrics;
    SecurityProfile security_profile;
    Location location;
};

struct ClientLocation {
    Point3D estimated_position;
    std::string connected_ap;
    double last_update = 0;
    double accuracy_meters = 0;
};

struct ClientMetrics {
    int rssi_dbm = 0;
    int snr_db = 0;
    int tx_rate_mbps = 0;
    int rx_rate_mbps = 0;
    double latency_ms = 0;
    uint64_t data_usage_bytes = 0;
    double last_updated = 0;
    double packet_loss_percent = 0;
    int retries = 0;
    int link_quality_percent = 0;
};

struct Client {
    std::string mac;
    std::string hostname;
    std::string ip_address;
    std::string connected_ap_mac;
    std::string connected_bssid;
    double connection_time = 0;
    std::string device_type;
    std::string manufacturer;
    double last_activity = 0;
    ClientMetrics client_metrics;
    ClientLocation location;
};

// ===== PERFORMANCE / ALARM MODELS =====

struct PerformanceAlarm {
    std::string id;
    std::string severity;
    std::string type;
    std::string message;
    double value = 0;
    double threshold = 0;
    double timestamp = 0;
    bool acknowledged = false;
};

struct ClientAlarm {
    std::string id;
    std::string client_mac;
    std::string severity;
    std::string type;
    std::string message;
    double value = 0;
    double threshold = 0;
    double timestamp = 0;
    bool acknowledged = false;
};

struct TimeSeriesMetric {
    double timestamp = 0;
    // Kept as a small fixed set matching what the Go version populated;
    // extend as needed rather than using a generic map for perf reasons.
    double cpu = 0, memory = 0, temperature = 0, power = 0;
    double tx_rate = 0, rx_rate = 0, error_rate = 0;
    double channel_util_24 = 0, channel_util_5 = 0;
    int active_clients = 0;
};

struct DevicePerformanceHistory {
    std::string device_mac;
    std::string device_name;
    std::vector<TimeSeriesMetric> metrics;
    std::vector<PerformanceAlarm> alarms;
    double last_updated = 0;
};

// ===== SYSTEM CONFIG =====

struct ControllerSettings {
    bool auto_optimization = true;
    bool channel_planning = true;
    bool power_management = true;
    bool firmware_management = true;
};

struct SecuritySettings {
    bool intrusion_detection = true;
    bool access_control = true;
    bool threat_protection = true;
    std::vector<std::string> allowed_macs;
    std::vector<std::string> blocked_macs;
};

struct SystemConfig {
    ControllerSettings controller_settings;
    SecuritySettings security_settings;
};

// ===== MESH TOPOLOGY =====

struct PerformanceMetrics {
    double average_throughput_mbps = 0;
    double average_latency_ms = 0;
    int total_clients = 0;
};

struct MeshTopology {
    std::string mesh_id;
    std::string controller_mac;
    int nodes = 0;
    std::string protocol;
    std::string version;
    PerformanceMetrics performance;
    double last_updated = 0;
};

// ===== WIRELESS / SSID CONFIG (mirrors HaulConfig in main.go) =====

struct HaulConfig {
    std::string haul_type;
    std::string ssid;
    std::string pass_phrase;
    bool enabled = false;
    std::vector<std::string> bands;
    std::string security_type;
    int vlan_id = 0;
};

// ===== RADIO / CHANNEL CONFIG =====

struct ClassInfo {
    int cls = 0;
    std::vector<int> supported_channels;
};

struct ChannelConfigEntry {
    std::string device_id;
    std::string radio_id;
    int radio_index = 0;
    int cls = 0;
    std::vector<int> channels;
    std::vector<int> preferences;
};

struct WifiChannelConfig {
    std::string device_id;
    std::vector<ClassInfo> supported_class;
    std::vector<ChannelConfigEntry> selected_config;
};

struct RadioConfig {
    std::string band;
    bool enabled = true;
    bool auto_channel = true;
    int channel = 0;
    int channel_width = 0;
    bool tx_power_auto = true;
    int tx_power_dbm = 0;
    std::string country_code = "US";
    int beacon_interval_ms = 100;
    int dtim_period = 2;
    int rts_threshold = 2347;
    int fragmentation_threshold = 2346;
    bool dfs_enabled = false;
    bool psc_only = false;
    std::vector<WifiChannelConfig> device_list;
};

struct BandSteeringConfig {
    bool enabled = true;
    std::string policy = "balanced";
    int rssi_threshold_2g4 = -70;
    int rssi_threshold_5g = -65;
    int rssi_threshold_6g = -60;
    double utilization_threshold_percent = 75.0;
    int block_time_seconds = 30;
    bool probe_response_suppression = true;
};

struct LoadBalancingConfig {
    bool enabled = true;
    std::string algorithm = "airtime_fairness";
    int rebalance_interval_seconds = 60;
    int client_count_threshold = 15;
    double utilization_threshold_percent = 70.0;
    int rssi_difference_threshold = 10;
};

struct AdvancedWirelessSettings {
    BandSteeringConfig band_steering;
    LoadBalancingConfig load_balancing;
    bool airtime_fairness = true;
    bool fast_transition = true;
    bool ofdma = true;
    bool mu_mimo = true;
    bool beamforming = true;
    bool twt = true;
    bool spatial_reuse = true;
    double updated_at = 0;
};

struct ChannelInfo {
    int channel = 0;
    int frequency_mhz = 0;
    bool dfs_required = false;
    int max_tx_power_dbm = 0;
    std::string availability;
    double utilization = 0;
    int noise_floor_dbm = 0;
};

struct BandScanResults {
    std::string band;
    std::vector<ChannelInfo> channels;
    double interference_level = 0;
    int average_noise_floor = 0;
};

struct ChannelRecommendation {
    int recommended_channel = 0;
    std::string reason;
    double expected_improvement_percent = 0;
};

struct ChannelScanResults {
    double scan_time = 0;
    int duration_seconds = 0;
    std::vector<std::pair<std::string, BandScanResults>> results;
    std::vector<std::pair<std::string, ChannelRecommendation>> recommendations;
};

// ===== COVERAGE / PLACEMENT =====

struct Point2D {
    double x = 0, y = 0;
};

struct WeakZone {
    std::string id;
    std::string points; // SVG polygon points, "x1,y1 x2,y2 ..."
    double area_m2 = 0;
    int average_rssi = 0;
    std::string severity;
    std::string reason;
    Point2D center;
};

struct PlacementSuggestion {
    std::string id;
    double x = 0, y = 0, z = 0;
    std::string device_type;
    double predicted_radius_m = 0;
    std::string predicted_quality;
    std::string interference_risk;
    double coverage_improvement_percent = 0;
    int priority = 0;
    std::string reason;
};

struct SignalPoint {
    double x = 0, y = 0;
    int rssi = -100;
    int snr = 0;
    std::string quality;
    std::vector<std::string> sources;
};

struct CoverageAnalysis {
    double total_coverage = 0;
    double excellent_coverage = 0;
    double good_coverage = 0;
    double fair_coverage = 0;
    double poor_coverage = 0;
    double weak_areas = 0;
    double dead_zones = 0;
    std::string interference_level;
    std::vector<WeakZone> weak_zones;
    std::vector<PlacementSuggestion> placement_suggestions;
    std::vector<std::vector<SignalPoint>> coverage_map; // only populated if requested
    double analyzed_at = 0;
};

struct CoverageRequest {
    std::string band = "2.4ghz";
    int threshold = -70;
    double map_scale = 0.1;
    int resolution = 50;
    bool include_heatmap = false;
};

struct OptimizationRequest {
    std::string band = "2.4ghz";
    double coverage_target = 95.0;
    int signal_threshold = -70;
    int max_devices = 0;
    double budget = 0;
};

struct Obstacle {
    std::string type;
    std::vector<Point2D> points;
    double attenuation_db = 0;
    std::string material;
};

struct FloorPlan {
    std::string id;
    std::string name;
    std::string url;
    int width_pixels = 1000;
    int height_pixels = 600;
    double scale_meters_per_pixel = 0.1;
    std::vector<Obstacle> obstacles;
    double created_at = 0;
    double updated_at = 0;
};

// ===== TOPOLOGY (rendered graph) =====

struct TopoSTA {
    std::string sta_mac;
    std::string client_type;
    std::string mld_addr;
    int band = 0;
    std::string ssid;
};

// Mirrors Go's BSS struct (json tags: BSSID, MLDAddr, vapMode, haulType,
// VlanId, Band, IEEE, ssid) — used inside TopoHaulType.bss_list, which the
// topology renderer (map.js) reads directly for the haul-type overlay
// circles (SSID label, VLAN, per-band BSSID/IEEE standard, MLD grouping).
struct TopoBSS {
    std::string bssid;
    std::string mld_addr;
    int vap_mode = 0;
    std::string haul_type;
    int vlan_id = 0;
    int band = 0;
    std::string ieee;
    std::string ssid;
};

// Mirrors Go's HaulTypeVisual struct — one entry per distinct HaulType
// (Fronthaul/Backhaul/Iot) present on a device's radios.
struct TopoHaulType {
    std::string name;
    std::string ssid;
    int vlan_id = 0;
    std::vector<TopoBSS> bss_list;
};

struct TopoNode {
    std::string id;
    std::string name;
    double x = 0, y = 0;
    std::vector<TopoSTA> sta_list;
    std::vector<TopoHaulType> haul_types;
};

struct TopoEdge {
    std::string from;
    std::string to;
    int band = -1;
    int channel = 0;
};

// ===== WIRELESS POLICY (mirrors wifiPolicyConfig in main.go) =====

struct APMetricReporting {
    int interval = 0;
    std::string managed_client_marker;
};

struct Default8021QSettings {
    int primary_vlan_id = 0;
    int default_pcp = 0;
};

struct UnsuccessfulAssociation {
    bool report_unsuccess_assoc = false;
    int max_reporting_rate = 0;
};

struct BackhaulBSSConfig {
    std::string bssid;
    bool profile1_bsta_disallowed = false;
    bool profile2_bsta_disallowed = false;
};

struct QoSManagementPolicy {
    std::vector<std::string> mscs_disallowed_sta_list;
    std::vector<std::string> scs_disallowed_sta_list;
};

struct RadioSpecificMetrics {
    std::string id;
    int sta_rcpi_threshold = 0;
    int sta_rcpi_hysteresis = 0;
    int ap_utilization_threshold = 0;
    int sta_traffic_stats = 0;
    int sta_link_metrics = 0;
    int sta_status = 0;
};

struct RadioSteeringParameters {
    std::string id;
    int steering_policy = 0;
    int utilization_threshold = 0;
    int rcpi_threshold = 0;
};

struct WifiPolicyConfig {
    std::string id;
    APMetricReporting ap_metric_reporting_policy;
    std::vector<std::string> local_steering_disallowed;
    std::vector<std::string> btm_steering_disallowed;
    int report_independent_channel_scans = 0;
    Default8021QSettings default_8021q_settings_policy;
    UnsuccessfulAssociation unsuccessful_assoc_policy;
    std::vector<BackhaulBSSConfig> backhaul_bss_config_policy;
    QoSManagementPolicy qos_management_policy;
    std::vector<RadioSpecificMetrics> radio_specific_metrics_policy;
    std::vector<RadioSteeringParameters> radio_steering_parameters_policy;
};
