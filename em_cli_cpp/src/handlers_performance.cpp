#include "handlers.h"
#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include <random>
#include <algorithm>
#include <cmath>

static std::mt19937& prng() { static std::mt19937 g(std::random_device{}()); return g; }
static double prand(double lo, double hi) { std::uniform_real_distribution<double> d(lo, hi); return d(prng()); }

static std::string classify_signal_quality_str(int rssi) {
    if (rssi >= -50) return "excellent";
    if (rssi >= -60) return "good";
    if (rssi >= -70) return "fair";
    return "poor";
}

static std::string calculate_connection_health(const ClientMetrics& m) {
    int score = 0;
    if (m.rssi_dbm >= -50) score += 40;
    else if (m.rssi_dbm >= -60) score += 30;
    else if (m.rssi_dbm >= -70) score += 20;
    else score += 10;

    if (m.latency_ms < 10) score += 30;
    else if (m.latency_ms < 30) score += 20;
    else if (m.latency_ms < 50) score += 10;

    if (m.packet_loss_percent < 0.1) score += 30;
    else if (m.packet_loss_percent < 1.0) score += 20;
    else if (m.packet_loss_percent < 5.0) score += 10;

    if (score >= 80) return "excellent";
    if (score >= 60) return "good";
    if (score >= 40) return "fair";
    return "poor";
}

static std::vector<Client> get_clients_for_device(const std::string& device_mac) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.clients_mu);
    std::vector<Client> out;
    for (auto& c : state.clients) if (c.connected_ap_mac == device_mac) out.push_back(c);
    return out;
}

static std::vector<ClientAlarm> get_client_alarms(const std::string& client_mac) {
    std::vector<ClientAlarm> alarms;
    auto& state = AppState::instance();
    std::shared_lock lock(state.clients_mu);
    const Client* client = nullptr;
    for (auto& c : state.clients) if (c.mac == client_mac) { client = &c; break; }
    if (!client) return alarms;

    double now = now_epoch();
    if (client->client_metrics.rssi_dbm < -70) {
        ClientAlarm a;
        a.id = "rssi-" + client_mac + "-" + std::to_string(long(now));
        a.client_mac = client_mac; a.severity = "warning"; a.type = "rssi";
        a.message = "Weak signal detected: " + std::to_string(client->client_metrics.rssi_dbm) + " dBm";
        a.value = client->client_metrics.rssi_dbm; a.threshold = -70; a.timestamp = now;
        alarms.push_back(a);
    }
    if (client->client_metrics.latency_ms > 50) {
        ClientAlarm a;
        a.id = "latency-" + client_mac + "-" + std::to_string(long(now));
        a.client_mac = client_mac; a.severity = "warning"; a.type = "latency";
        a.message = "High latency detected: " + std::to_string(client->client_metrics.latency_ms) + " ms";
        a.value = client->client_metrics.latency_ms; a.threshold = 50; a.timestamp = now;
        alarms.push_back(a);
    }
    if (client->client_metrics.packet_loss_percent > 1.0) {
        ClientAlarm a;
        a.id = "packetloss-" + client_mac + "-" + std::to_string(long(now));
        a.client_mac = client_mac; a.severity = "critical"; a.type = "packet_loss";
        a.message = "High packet loss: " + std::to_string(client->client_metrics.packet_loss_percent) + "%";
        a.value = client->client_metrics.packet_loss_percent; a.threshold = 1.0; a.timestamp = now;
        alarms.push_back(a);
    }
    return alarms;
}

static std::vector<TimeSeriesMetric> generate_client_historical_metrics(const std::string& client_mac, int count) {
    std::vector<TimeSeriesMetric> metrics;
    int base_rssi = -55, base_snr = 45, base_tx_rate = 800;
    double base_latency = 8.0, base_packet_loss = 0.2;

    {
        auto& state = AppState::instance();
        std::shared_lock lock(state.clients_mu);
        for (auto& c : state.clients) {
            if (c.mac == client_mac) {
                base_rssi = c.client_metrics.rssi_dbm;
                base_snr = c.client_metrics.snr_db;
                base_tx_rate = c.client_metrics.tx_rate_mbps;
                base_latency = c.client_metrics.latency_ms;
                base_packet_loss = c.client_metrics.packet_loss_percent;
                break;
            }
        }
    }

    double now = now_epoch();
    for (int i = count - 1; i >= 0; i--) {
        TimeSeriesMetric m;
        m.timestamp = now - i * 10;
        // Reuse fields loosely to carry client-style metrics through the
        // same TimeSeriesMetric shape used for device metrics.
        m.cpu = base_rssi + prand(-4, 4);   // rssi
        m.memory = base_snr + prand(-3, 3); // snr
        m.tx_rate = std::max(100.0, base_tx_rate + prand(-100, 100));
        m.rx_rate = m.tx_rate;
        m.error_rate = std::max(0.0, base_packet_loss + prand(-0.15, 0.15));
        m.temperature = std::max(0.5, base_latency + prand(-2, 2)); // latency
        metrics.push_back(m);
    }
    return metrics;
}

static std::vector<TimeSeriesMetric> generate_enhanced_historical_metrics(int count, double base_load) {
    std::vector<TimeSeriesMetric> metrics;
    double now = now_epoch();
    for (int i = count - 1; i >= 0; i--) {
        TimeSeriesMetric m;
        m.timestamp = now - i * 10;
        m.cpu = std::min(95.0, base_load + (i % 20) + prand(0, 10));
        m.memory = std::min(90.0, 45.0 + (i % 15) + prand(0, 8));
        m.temperature = std::min(75.0, 40.0 + (i % 10) + prand(0, 5));
        m.power = 18.0 + prand(0, 8);
        m.tx_rate = 100.0 + prand(0, 400);
        m.rx_rate = 150.0 + prand(0, 500);
        m.error_rate = prand(0, 0.5);
        m.channel_util_24 = 30.0 + prand(0, 40);
        m.channel_util_5 = 20.0 + prand(0, 30);
        m.active_clients = 2 + int(prand(0, 4));
        metrics.push_back(m);
    }
    return metrics;
}

MHD_Result handle_get_all_devices_performance(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::vector<Device> devices_copy;
    { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& d : devices_copy) {
        auto clients = get_clients_for_device(d.mac);
        DevicePerformanceHistory* history = nullptr;
        { std::shared_lock lock(state.perf_mu); history = state.find_history(d.mac); }

        cJSON* entry = cJSON_CreateObject();
        add_str(entry, "device_mac", d.mac);
        add_str(entry, "device_name", d.model);
        add_str(entry, "device_role", d.role);
        add_str(entry, "device_status", d.status);
        cJSON_AddNumberToObject(entry, "client_count", (double)clients.size());
        cJSON_AddItemToObject(entry, "metrics", device_metrics_to_json(d.metrics));
        cJSON* alarms = cJSON_CreateArray();
        if (history) for (auto& a : history->alarms) cJSON_AddItemToArray(alarms, performance_alarm_to_json(a));
        cJSON_AddItemToObject(entry, "alarms", alarms);
        add_str(entry, "last_updated", epoch_to_rfc3339(history ? history->last_updated : now_epoch()));
        cJSON_AddItemToArray(arr, entry);
    }
    cJSON_AddItemToObject(root.get(), "devices", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

static cJSON* client_performance_data_to_json(const Client& client) {
    cJSON* o = cJSON_CreateObject();
    add_str(o, "mac", client.mac);
    add_str(o, "hostname", client.hostname);
    add_str(o, "device_type", client.device_type);

    cJSON* cm = cJSON_CreateObject();
    cJSON_AddNumberToObject(cm, "rssi_dbm", client.client_metrics.rssi_dbm);
    cJSON_AddNumberToObject(cm, "snr_db", client.client_metrics.snr_db);
    cJSON_AddNumberToObject(cm, "tx_rate_mbps", client.client_metrics.tx_rate_mbps);
    cJSON_AddNumberToObject(cm, "rx_rate_mbps", client.client_metrics.rx_rate_mbps);
    cJSON_AddNumberToObject(cm, "latency_ms", client.client_metrics.latency_ms);
    cJSON_AddNumberToObject(cm, "packet_loss_percent", client.client_metrics.packet_loss_percent);
    cJSON_AddNumberToObject(cm, "retries", client.client_metrics.retries);
    cJSON_AddNumberToObject(cm, "link_quality_percent", client.client_metrics.link_quality_percent);
    cJSON_AddNumberToObject(cm, "data_rate_mbps", (client.client_metrics.tx_rate_mbps + client.client_metrics.rx_rate_mbps) / 2.0);
    add_str(cm, "signal_quality", classify_signal_quality_str(client.client_metrics.rssi_dbm));
    add_str(cm, "timestamp", epoch_to_rfc3339(now_epoch()));
    cJSON_AddItemToObject(o, "current_metrics", cm);

    cJSON* hist = cJSON_CreateArray();
    for (auto& m : generate_client_historical_metrics(client.mac, 50)) cJSON_AddItemToArray(hist, timeseries_metric_to_json(m));
    cJSON_AddItemToObject(o, "history", hist);

    cJSON* alarms = cJSON_CreateArray();
    for (auto& a : get_client_alarms(client.mac)) cJSON_AddItemToArray(alarms, client_alarm_to_json(a));
    cJSON_AddItemToObject(o, "alarms", alarms);

    add_str(o, "connection_health", calculate_connection_health(client.client_metrics));
    add_str(o, "last_updated", epoch_to_rfc3339(now_epoch()));
    return o;
}

MHD_Result handle_get_device_performance(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    DevicePerformanceHistory* history = nullptr;
    { std::shared_lock lock(state.perf_mu); history = state.find_history(mac); }
    if (!history) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Device not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }

    const Device* device = nullptr;
    { std::shared_lock lock(state.devices_mu); for (auto& d : state.devices) if (d.mac == mac) { device = &d; break; } }
    if (!device) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Device not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }

    auto clients = get_clients_for_device(mac);

    CJsonPtr root(cJSON_CreateObject());
    cJSON* dev = cJSON_CreateObject();
    add_str(dev, "mac", device->mac);
    add_str(dev, "name", device->model);
    add_str(dev, "role", device->role);
    add_str(dev, "status", device->status);
    cJSON_AddItemToObject(dev, "metrics", device_metrics_to_json(device->metrics));
    cJSON_AddItemToObject(root.get(), "device", dev);

    cJSON* client_arr = cJSON_CreateArray();
    for (auto& c : clients) cJSON_AddItemToArray(client_arr, client_performance_data_to_json(c));
    cJSON_AddItemToObject(root.get(), "clients", client_arr);

    cJSON* metrics_arr = cJSON_CreateArray();
    auto metrics_to_use = history->metrics.empty() ? generate_enhanced_historical_metrics(50, 30.0) : history->metrics;
    for (auto& m : metrics_to_use) cJSON_AddItemToArray(metrics_arr, timeseries_metric_to_json(m));
    cJSON_AddItemToObject(root.get(), "metrics", metrics_arr);

    cJSON* alarms_arr = cJSON_CreateArray();
    for (auto& a : history->alarms) cJSON_AddItemToArray(alarms_arr, performance_alarm_to_json(a));
    cJSON_AddItemToObject(root.get(), "alarms", alarms_arr);

    add_str(root.get(), "last_updated", epoch_to_rfc3339(history->last_updated));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_device_clients_performance(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    const Device* device = nullptr;
    { std::shared_lock lock(state.devices_mu); for (auto& d : state.devices) if (d.mac == mac) { device = &d; break; } }
    if (!device) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Device not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    auto clients = get_clients_for_device(mac);

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "device_mac", mac);
    cJSON* arr = cJSON_CreateArray();
    for (auto& c : clients) cJSON_AddItemToArray(arr, client_performance_data_to_json(c));
    cJSON_AddItemToObject(root.get(), "clients", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_client_performance(struct MHD_Connection* connection, const std::string& mac) {
    auto& state = AppState::instance();
    const Client* client = nullptr;
    { std::shared_lock lock(state.clients_mu); for (auto& c : state.clients) if (c.mac == mac) { client = &c; break; } }
    if (!client) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Client not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    CJsonPtr root(client_performance_data_to_json(*client));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_performance_alarms(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    CJsonPtr root(cJSON_CreateObject());
    cJSON* all = cJSON_CreateArray();

    {
        std::shared_lock lock(state.perf_mu);
        for (auto& h : state.performance_history) {
            for (auto& a : h.alarms) {
                cJSON* entry = cJSON_CreateObject();
                add_str(entry, "alarm_type", "device");
                add_str(entry, "device_mac", h.device_mac);
                cJSON_AddItemToObject(entry, "alarm", performance_alarm_to_json(a));
                cJSON_AddItemToArray(all, entry);
            }
        }
    }
    {
        std::shared_lock lock(state.clients_mu);
        for (auto& c : state.clients) {
            for (auto& a : get_client_alarms(c.mac)) {
                cJSON* entry = cJSON_CreateObject();
                add_str(entry, "alarm_type", "client");
                add_str(entry, "client_mac", c.mac);
                add_str(entry, "device_mac", c.connected_ap_mac);
                cJSON_AddItemToObject(entry, "alarm", client_alarm_to_json(a));
                cJSON_AddItemToArray(all, entry);
            }
        }
    }

    cJSON_AddItemToObject(root.get(), "alarms", all);
    cJSON_AddNumberToObject(root.get(), "total", cJSON_GetArraySize(all));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_acknowledge_alarm(struct MHD_Connection* connection, const std::string& id) {
    auto& state = AppState::instance();
    bool acknowledged = false;
    {
        std::unique_lock lock(state.perf_mu);
        for (auto& h : state.performance_history) {
            for (auto& a : h.alarms) {
                if (a.id == id) { a.acknowledged = true; acknowledged = true; break; }
            }
            if (acknowledged) break;
        }
    }
    if (!acknowledged) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Alarm not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Alarm acknowledged");
    add_str(root.get(), "alarm_id", id);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Metrics =====

MHD_Result handle_get_device_metrics(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.devices_mu);
    CJsonPtr root(cJSON_CreateObject());
    cJSON* metrics = cJSON_CreateObject();
    for (auto& d : state.devices) cJSON_AddItemToObject(metrics, d.mac.c_str(), device_metrics_to_json(d.metrics));
    cJSON_AddItemToObject(root.get(), "metrics", metrics);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_client_metrics(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.clients_mu);
    CJsonPtr root(cJSON_CreateObject());
    cJSON* metrics = cJSON_CreateObject();
    for (auto& c : state.clients) cJSON_AddItemToObject(metrics, c.mac.c_str(), client_metrics_to_json(c.client_metrics));
    cJSON_AddItemToObject(root.get(), "metrics", metrics);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_performance_metrics(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    int total_clients = 0;
    { std::shared_lock lock(state.clients_mu); total_clients = static_cast<int>(state.clients.size()); }

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "average_throughput_mbps", 847.5);
    cJSON_AddNumberToObject(root.get(), "average_latency_ms", 3.2);
    cJSON_AddNumberToObject(root.get(), "total_clients", total_clients);
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_interference_analysis(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    cJSON* bands = cJSON_CreateObject();

    cJSON* b24 = cJSON_CreateObject();
    cJSON_AddNumberToObject(b24, "interference_level", 0.15);
    cJSON_AddNumberToObject(b24, "noise_floor", -95);
    cJSON* s24 = cJSON_CreateArray();
    cJSON_AddItemToArray(s24, cJSON_CreateString("Microwave Oven"));
    cJSON_AddItemToArray(s24, cJSON_CreateString("Bluetooth Devices"));
    cJSON_AddItemToObject(b24, "sources", s24);
    cJSON_AddItemToObject(bands, "2.4GHz", b24);

    cJSON* b5 = cJSON_CreateObject();
    cJSON_AddNumberToObject(b5, "interference_level", 0.08);
    cJSON_AddNumberToObject(b5, "noise_floor", -98);
    cJSON* s5 = cJSON_CreateArray();
    cJSON_AddItemToArray(s5, cJSON_CreateString("Neighboring APs"));
    cJSON_AddItemToObject(b5, "sources", s5);
    cJSON_AddItemToObject(bands, "5GHz", b5);

    cJSON* b6 = cJSON_CreateObject();
    cJSON_AddNumberToObject(b6, "interference_level", 0.02);
    cJSON_AddNumberToObject(b6, "noise_floor", -100);
    cJSON_AddItemToObject(b6, "sources", cJSON_CreateArray());
    cJSON_AddItemToObject(bands, "6GHz", b6);

    cJSON_AddItemToObject(root.get(), "bands", bands);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}
