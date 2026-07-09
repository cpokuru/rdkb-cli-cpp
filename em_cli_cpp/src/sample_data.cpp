#include "app_state.h"

// Seeds the same 3 devices the Go version used as fallback data when
// /nvram/static/devices.json isn't present. Real device/client loading
// from the em_cli C library (via em_cli_apis.h) replaces this in the
// next batch — this exists so the server is runnable/testable now.
void seed_sample_data() {
    auto& state = AppState::instance();
    double now = now_epoch();

    {
        std::unique_lock lock(state.devices_mu);
        state.devices = {
            Device{
                .mac = "AA:BB:CC:00:00:01", .role = "Controller", .vendor = "OpenSync",
                .model = "EasyMesh-R6-Pro", .ip_address = "192.168.1.1", .status = "Online",
                .last_seen = now, .uptime = "7d 3h 42m",
                .capabilities = { .wifi7_support = true, .max_mesh_links = 8,
                                   .firmware = "v6.2.1-easymesh-r6", .serial_number = "ESM001R6PRO",
                                   .supported_bands = {"2.4GHz", "5GHz", "6GHz"} },
                .metrics = { .cpu_usage_percent = 32.1, .memory_usage_percent = 48.0,
                             .temperature_celsius = 41.5, .last_updated = now },
                .security_profile = { .profile_name = "Enterprise-Grade", .auth_method = "WPA3-SAE",
                                        .encryption_type = "AES-256", .security_level = "High" },
                .location = { .building = "Main House", .floor = "1st Floor", .room = "Network Closet",
                               .description = "Primary controller", .position_3d = {0.0, 0.0, 0.8} }
            },
            Device{
                .mac = "AA:BB:CC:00:00:02", .role = "Agent", .vendor = "Plume",
                .model = "SuperPod-R6", .ip_address = "192.168.1.10", .status = "Online",
                .last_seen = now, .uptime = "6d 12h 18m",
                .capabilities = { .wifi7_support = true, .max_mesh_links = 4,
                                   .firmware = "v3.1.2-plume-r6", .serial_number = "PLM002SP6",
                                   .supported_bands = {"5GHz", "6GHz"} },
                .metrics = { .cpu_usage_percent = 27.3, .memory_usage_percent = 45.0,
                             .temperature_celsius = 39.0, .last_updated = now },
                .security_profile = { .profile_name = "Consumer-Premium", .auth_method = "WPA3-SAE",
                                        .encryption_type = "AES-256", .security_level = "High" },
                .location = { .building = "Main House", .floor = "1st Floor", .room = "Living Room",
                               .description = "Wall-mounted agent", .position_3d = {5.0, 0.0, 1.5} }
            },
            Device{
                .mac = "AA:BB:CC:00:00:03", .role = "Agent", .vendor = "Google",
                .model = "Nest Wifi Pro 6E R6", .ip_address = "192.168.1.11", .status = "Online",
                .last_seen = now, .uptime = "5d 8h 26m",
                .capabilities = { .wifi7_support = false, .max_mesh_links = 6,
                                   .firmware = "v1.9.3-nest-r6", .serial_number = "NST003P6E",
                                   .supported_bands = {"2.4GHz", "5GHz", "6GHz"} },
                .metrics = { .cpu_usage_percent = 35.0, .memory_usage_percent = 50.0,
                             .temperature_celsius = 42.0, .last_updated = now },
                .security_profile = { .profile_name = "Standard", .auth_method = "WPA3-SAE",
                                        .encryption_type = "AES-256", .security_level = "Medium" },
                .location = { .building = "Main House", .floor = "2nd Floor", .room = "Master Bedroom",
                               .description = "Bedside placement", .position_3d = {8.0, 4.0, 4.2} }
            }
        };
    }

    {
        std::unique_lock lock(state.clients_mu);
        state.clients = {
            Client{
                .mac = "44:85:00:12:34:56", .hostname = "MacBook Pro M3", .ip_address = "192.168.1.101",
                .connected_ap_mac = "AA:BB:CC:00:00:01", .connected_bssid = "AA:BB:CC:00:00:03",
                .connection_time = now - 7200, .device_type = "laptop", .manufacturer = "Apple Inc.",
                .last_activity = now,
                .client_metrics = { .rssi_dbm = -42, .snr_db = 56, .tx_rate_mbps = 1201, .rx_rate_mbps = 1201,
                                      .latency_ms = 2.1, .data_usage_bytes = 10737418240ULL, .last_updated = now,
                                      .packet_loss_percent = 0.02, .retries = 5, .link_quality_percent = 98 },
                .location = { .estimated_position = {2.5, -1.0, 1.2}, .connected_ap = "AA:BB:CC:00:00:01",
                               .last_update = now, .accuracy_meters = 3.2 }
            }
        };
    }
}
