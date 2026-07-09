#pragma once

#include <vector>
#include <mutex>
#include <shared_mutex>
#include "models.h"

// Mirrors the Go globals (var devices []Device, var clients []Client, etc.)
// Go's sync.Mutex / sync.RWMutex map directly to std::mutex / std::shared_mutex.
class AppState {
public:
    std::shared_mutex devices_mu;
    std::vector<Device> devices;

    std::shared_mutex clients_mu;
    std::vector<Client> clients;

    std::mutex topology_mu;
    MeshTopology topology;

    std::mutex config_mu;
    SystemConfig system_config;

    std::shared_mutex perf_mu;
    std::vector<DevicePerformanceHistory> performance_history; // keyed by mac, linear scan (device count is small)

    DevicePerformanceHistory* find_history(const std::string& mac) {
        for (auto& h : performance_history) if (h.device_mac == mac) return &h;
        return nullptr;
    }

    // Wireless config (matches radioConfigs / advancedSettings / lastScanResults globals)
    std::shared_mutex wireless_mu;
    std::vector<std::pair<std::string, RadioConfig>> radio_configs; // band -> config
    AdvancedWirelessSettings advanced_settings;
    ChannelScanResults last_scan_results;
    bool has_scan_results = false;

    // Coverage / floor plans
    std::shared_mutex coverage_mu;
    CoverageAnalysis current_coverage;
    bool has_current_coverage = false;
    std::vector<FloorPlan> floor_plans;

    RadioConfig* find_radio_config(const std::string& band) {
        for (auto& p : radio_configs) if (p.first == band) return &p.second;
        return nullptr;
    }
    FloorPlan* find_floor_plan(const std::string& id) {
        for (auto& f : floor_plans) if (f.id == id) return &f;
        return nullptr;
    }

    static AppState& instance() {
        static AppState s;
        return s;
    }

private:
    AppState() = default;
};
