#pragma once
#include "http_server.h"
#include <string>

// Wireless (radios, advanced settings, scan, aggregate config, policy)
MHD_Result handle_get_radios(struct MHD_Connection*);
MHD_Result handle_post_radios(struct MHD_Connection*, const std::string& body);
MHD_Result handle_put_radio(struct MHD_Connection*, const std::string& band, const std::string& body);
MHD_Result handle_get_advanced(struct MHD_Connection*);
MHD_Result handle_put_advanced(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_scan(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_scan_results(struct MHD_Connection*);
MHD_Result handle_get_wireless_config(struct MHD_Connection*);
MHD_Result handle_put_wireless_config(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_wifi_policy(struct MHD_Connection*);
MHD_Result handle_post_wifi_policy(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_wifi_reset(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_wifi_reset(struct MHD_Connection*);
MHD_Result handle_post_unassoc_sta_query(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_controller_ip(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_controller_ip(struct MHD_Connection*);

// Coverage / placement
MHD_Result handle_get_coverage_analysis(struct MHD_Connection*);
MHD_Result handle_post_coverage_analyze(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_coverage_optimize(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_floorplans(struct MHD_Connection*);
MHD_Result handle_post_floorplans(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_floorplan(struct MHD_Connection*, const std::string& id);
MHD_Result handle_put_floorplan(struct MHD_Connection*, const std::string& id, const std::string& body);
MHD_Result handle_delete_floorplan(struct MHD_Connection*, const std::string& id);
MHD_Result handle_get_heatmap(struct MHD_Connection*);
MHD_Result handle_get_band_heatmap(struct MHD_Connection*, const std::string& band);
MHD_Result handle_post_simulate_placement(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_predict_placement(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_weak_zones(struct MHD_Connection*);
MHD_Result handle_get_dead_spots(struct MHD_Connection*);
MHD_Result handle_get_coverage_report(struct MHD_Connection*);
MHD_Result handle_get_coverage_report_pdf(struct MHD_Connection*);

// Topology
MHD_Result handle_get_topology(struct MHD_Connection*);
MHD_Result handle_post_topology_optimize(struct MHD_Connection*);

// Performance / metrics
MHD_Result handle_get_all_devices_performance(struct MHD_Connection*);
MHD_Result handle_get_device_performance(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_get_device_clients_performance(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_get_client_performance(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_get_performance_alarms(struct MHD_Connection*);
MHD_Result handle_post_acknowledge_alarm(struct MHD_Connection*, const std::string& id);
MHD_Result handle_get_device_metrics(struct MHD_Connection*);
MHD_Result handle_get_client_metrics(struct MHD_Connection*);
MHD_Result handle_get_performance_metrics(struct MHD_Connection*);
MHD_Result handle_get_interference_analysis(struct MHD_Connection*);

// Misc: config, security, firmware, reports, system logs, device/client mutations
MHD_Result handle_get_config(struct MHD_Connection*);
MHD_Result handle_put_config(struct MHD_Connection*, const std::string& body);
MHD_Result handle_get_security_profiles(struct MHD_Connection*);
MHD_Result handle_get_threat_analysis(struct MHD_Connection*);
MHD_Result handle_get_firmware_status(struct MHD_Connection*);
MHD_Result handle_post_firmware_update(struct MHD_Connection*);
MHD_Result handle_get_usage_report(struct MHD_Connection*);
MHD_Result handle_get_performance_report(struct MHD_Connection*);
MHD_Result handle_get_system_logs(struct MHD_Connection*);
MHD_Result handle_post_reboot_device(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_post_disconnect_client(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_post_block_client(struct MHD_Connection*, const std::string& mac);
MHD_Result handle_post_unblock_client(struct MHD_Connection*, const std::string& mac);

// Called once at startup (mirrors init() in main.go): seeds wireless
// settings, floor plans, and performance history defaults.
void init_wireless_and_coverage_defaults();

// rbus explorer (generic debug/inspection endpoints — see rbus_bridge.h)
MHD_Result handle_get_rbus_status(struct MHD_Connection*);
MHD_Result handle_get_rbus_components(struct MHD_Connection*, const std::string& path);
MHD_Result handle_get_rbus_elements(struct MHD_Connection*, const std::string& component);
MHD_Result handle_post_rbus_get(struct MHD_Connection*, const std::string& body);
MHD_Result handle_post_rbus_method_invoke(struct MHD_Connection*, const std::string& body);

// Periodic background task (10s cadence) that appends metric history points
// and raises/expires alarms — mirrors updatePerformanceHistory() in main.go.
void start_performance_background_task();

// Periodic background task (10s cadence) that refreshes AppState's
// devices/clients from the real TR-181 data model via rbus (see
// rbus_datamodel_bridge.cpp) — replaces the old sample_data.cpp hardcoded
// values as the live source, per the full-replace-exec()-with-rbus
// decision. Every handler that reads AppState.devices/clients (performance
// tracking, metrics, health score, WS broadcasts) keeps working unchanged,
// just backed by real data now instead of static fixtures.
void start_rbus_refresh_task();
