#include "coverage.h"
#include <cmath>
#include <random>
#include <algorithm>
#include <sstream>
#include <queue>

namespace coverage {

static std::mt19937& rng() {
    static std::mt19937 gen(std::random_device{}());
    return gen;
}
static double rand01() {
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng());
}

static std::vector<Device> get_online_devices(const std::vector<Device>& devices) {
    std::vector<Device> out;
    for (auto& d : devices) if (d.status == "Online") out.push_back(d);
    return out;
}

static Point2D get_device_map_position(const Device& device) {
    if (device.location.position_3d.x != 0 || device.location.position_3d.y != 0) {
        double x = (device.location.position_3d.x / 0.1) + 500;
        double y = (device.location.position_3d.y / 0.1) + 300;
        return { std::max(0.0, std::min(1000.0, x)), std::max(0.0, std::min(600.0, y)) };
    }
    static const std::vector<std::pair<std::string, Point2D>> defaults = {
        {"AA:BB:CC:00:00:01", {200, 300}},
        {"AA:BB:CC:00:00:02", {500, 200}},
        {"AA:BB:CC:00:00:03", {750, 400}},
    };
    for (auto& [mac, pos] : defaults) if (mac == device.mac) return pos;
    return {100, 100};
}

static double euclidean(double x1, double y1, double x2, double y2) {
    return std::sqrt(std::pow(x2 - x1, 2) + std::pow(y2 - y1, 2));
}

static double band_frequency_mhz(const std::string& band) {
    if (band == "2.4ghz") return 2400.0;
    if (band == "5ghz") return 5000.0;
    if (band == "6ghz") return 6000.0;
    return 2400.0;
}

static double environmental_loss(double distance, const std::string& band) {
    double loss = 0.0;
    if (distance > 10) loss += 2.0 * std::log10(distance / 10);
    if (band == "2.4ghz") loss += 2.0;
    else if (band == "5ghz") loss += 5.0;
    else if (band == "6ghz") loss += 8.0;
    double variation = (rand01() - 0.5) * 4.0;
    loss += variation;
    return std::max(0.0, loss);
}

static int path_loss(const Device& device, double distance_m, const std::string& band) {
    double tx_power = 20.0;
    if (device.metrics.power_consumption_watts > 0) {
        if (band == "2.4ghz") tx_power = 20.0;
        else if (band == "5ghz") tx_power = 24.0;
        else if (band == "6ghz") tx_power = 30.0;
    }
    double frequency = band_frequency_mhz(band);
    double distance_km = std::max(distance_m / 1000.0, 0.001);
    double fspl = 20 * std::log10(distance_km) + 20 * std::log10(frequency) + 32.45;
    double env_loss = environmental_loss(distance_m, band);
    double rssi = tx_power - fspl - env_loss;
    return static_cast<int>(std::max(-100.0, std::min(0.0, rssi)));
}

static std::string classify_signal_quality(int rssi) {
    if (rssi >= -50) return "excellent";
    if (rssi >= -60) return "good";
    if (rssi >= -70) return "fair";
    if (rssi >= -80) return "poor";
    return "none";
}

static SignalPoint calculate_signal_at_point(double x, double y, const std::vector<Device>& devices,
                                                const std::string& band, double /*map_scale*/) {
    SignalPoint point;
    point.x = x; point.y = y; point.rssi = -100;
    int max_rssi = -100;
    std::vector<std::string> sources;

    for (auto& device : devices) {
        if (device.status != "Online") continue;
        Point2D pos = get_device_map_position(device);
        double distance = euclidean(x, y, pos.x, pos.y) * 0.1; // mapScale
        if (distance < 0.1) distance = 0.1;

        int rssi = path_loss(device, distance, band);
        if (rssi > max_rssi) {
            max_rssi = rssi;
            sources.push_back(device.mac);
        }
        if (rssi > -90) sources.push_back(device.mac);
    }

    point.rssi = max_rssi;
    point.sources = sources;
    point.quality = classify_signal_quality(max_rssi);

    int noise_floor = -95;
    if (band == "5ghz") noise_floor = -98;
    else if (band == "6ghz") noise_floor = -100;
    point.snr = max_rssi - noise_floor;

    return point;
}

static std::string interference_level_str(const std::vector<std::vector<SignalPoint>>& grid) {
    double total = 0.0;
    int points = 0;
    for (auto& row : grid) {
        for (auto& p : row) {
            if (p.sources.size() > 1) total += static_cast<double>(p.sources.size() - 1);
            points++;
        }
    }
    double avg = points > 0 ? total / points : 0.0;
    if (avg > 2.0) return "High";
    if (avg > 1.0) return "Medium";
    return "Low";
}

static CoverageAnalysis calculate_coverage_statistics(const std::vector<std::vector<SignalPoint>>& grid, int /*threshold*/) {
    int total = 0, excellent = 0, good = 0, fair = 0, poor = 0, none = 0;
    for (auto& row : grid) {
        total += static_cast<int>(row.size());
        for (auto& p : row) {
            if (p.quality == "excellent") excellent++;
            else if (p.quality == "good") good++;
            else if (p.quality == "fair") fair++;
            else if (p.quality == "poor") poor++;
            else if (p.quality == "none") none++;
        }
    }
    CoverageAnalysis a;
    if (total > 0) {
        a.total_coverage = double(total - none) / total * 100;
        a.excellent_coverage = double(excellent) / total * 100;
        a.good_coverage = double(good) / total * 100;
        a.fair_coverage = double(fair) / total * 100;
        a.poor_coverage = double(poor) / total * 100;
        a.weak_areas = double(poor) / total * 100;
        a.dead_zones = double(none) / total * 100;
    }
    a.interference_level = interference_level_str(grid);
    return a;
}

static std::string classify_zone_severity(int avg_rssi) {
    if (avg_rssi < -90) return "critical";
    if (avg_rssi < -85) return "high";
    if (avg_rssi < -80) return "medium";
    return "low";
}

static std::string weak_zone_reason(int avg_rssi) {
    if (avg_rssi < -90) return "Dead zone - no usable signal";
    if (avg_rssi < -85) return "Very weak signal - connectivity issues likely";
    if (avg_rssi < -80) return "Weak signal - reduced performance";
    return "Marginal signal quality";
}

static Point2D centroid(const std::vector<Point2D>& points) {
    if (points.empty()) return {};
    double sx = 0, sy = 0;
    for (auto& p : points) { sx += p.x; sy += p.y; }
    return { sx / points.size(), sy / points.size() };
}

static WeakZone explore_weak_zone(const std::vector<std::vector<SignalPoint>>& grid,
                                     std::vector<std::vector<bool>>& visited,
                                     int start_x, int start_y, int threshold, double map_scale) {
    std::vector<Point2D> points;
    int rssi_sum = 0, count = 0;
    std::queue<std::pair<int,int>> q;
    q.push({start_x, start_y});
    int rows = static_cast<int>(grid.size());
    int cols = rows > 0 ? static_cast<int>(grid[0].size()) : 0;

    while (!q.empty()) {
        auto [x, y] = q.front(); q.pop();
        if (x < 0 || x >= rows || y < 0 || y >= cols || visited[x][y]) continue;
        if (grid[x][y].rssi >= threshold) continue;
        visited[x][y] = true;

        double map_x = (double(x) / std::max(1, rows - 1)) * 1000;
        double map_y = (double(y) / std::max(1, cols - 1)) * 600;
        points.push_back({map_x, map_y});
        rssi_sum += grid[x][y].rssi;
        count++;

        q.push({x - 1, y}); q.push({x + 1, y}); q.push({x, y - 1}); q.push({x, y + 1});
    }

    WeakZone zone;
    int avg_rssi = count > 0 ? rssi_sum / count : -100;
    zone.area_m2 = count * std::pow(map_scale * 20, 2);
    zone.average_rssi = avg_rssi;
    zone.severity = classify_zone_severity(avg_rssi);
    zone.reason = weak_zone_reason(avg_rssi);
    zone.center = centroid(points);

    std::ostringstream oss;
    for (size_t i = 0; i < points.size(); i++) {
        if (i > 0) oss << " ";
        oss << points[i].x << "," << points[i].y;
    }
    zone.points = oss.str();
    return zone;
}

static std::vector<WeakZone> identify_weak_zones(const std::vector<std::vector<SignalPoint>>& grid,
                                                     int threshold, double map_scale) {
    std::vector<WeakZone> zones;
    if (grid.empty()) return zones;
    int rows = static_cast<int>(grid.size());
    int cols = static_cast<int>(grid[0].size());
    std::vector<std::vector<bool>> visited(rows, std::vector<bool>(cols, false));

    int zone_id = 1;
    for (int x = 0; x < rows; x++) {
        for (int y = 0; y < cols; y++) {
            if (!visited[x][y] && grid[x][y].rssi < threshold) {
                WeakZone zone = explore_weak_zone(grid, visited, x, y, threshold, map_scale);
                if (zone.area_m2 > 1.0) {
                    zone.id = "weak_zone_" + std::to_string(zone_id++);
                    zones.push_back(std::move(zone));
                }
            }
        }
    }
    return zones;
}

CoverageAnalysis analyze(const CoverageRequest& request, const std::vector<Device>& devices) {
    CoverageAnalysis analysis;
    analysis.analyzed_at = now_epoch();

    auto online = get_online_devices(devices);
    if (online.empty()) return analysis; // matches Go's early return with empty analysis

    int grid_size = request.resolution > 0 ? request.resolution : 50;
    std::vector<std::vector<SignalPoint>> grid(grid_size, std::vector<SignalPoint>(grid_size));

    double map_width = 1000.0, map_height = 600.0;
    for (int x = 0; x < grid_size; x++) {
        for (int y = 0; y < grid_size; y++) {
            double px = (double(x) / std::max(1, grid_size - 1)) * map_width;
            double py = (double(y) / std::max(1, grid_size - 1)) * map_height;
            grid[x][y] = calculate_signal_at_point(px, py, online, request.band, request.map_scale);
        }
    }

    analysis = calculate_coverage_statistics(grid, request.threshold);
    analysis.analyzed_at = now_epoch();
    analysis.weak_zones = identify_weak_zones(grid, request.threshold, request.map_scale);
    if (request.include_heatmap) analysis.coverage_map = grid;

    return analysis;
}

static double estimate_coverage_improvement(Point2D position, const std::vector<Device>& devices) {
    double nearest = 1e18;
    for (auto& d : devices) {
        Point2D pos = get_device_map_position(d);
        double dist = euclidean(position.x, position.y, pos.x, pos.y);
        if (dist < nearest) nearest = dist;
    }
    if (nearest > 200) return 20.0;
    if (nearest > 100) return 12.0;
    if (nearest > 50) return 8.0;
    return 3.0;
}

static double predicted_radius(const std::string& band) {
    if (band == "2.4ghz") return 30.0;
    if (band == "5ghz") return 25.0;
    if (band == "6ghz") return 20.0;
    return 25.0;
}

static std::vector<PlacementSuggestion> find_coverage_gaps(const std::vector<Device>& online, const OptimizationRequest& request) {
    std::vector<PlacementSuggestion> suggestions;
    static const std::vector<Point2D> candidates = {
        {200, 150}, {800, 150}, {200, 450}, {800, 450}, {500, 300}
    };
    int i = 0;
    for (auto& c : candidates) {
        i++;
        double improvement = estimate_coverage_improvement(c, online);
        if (improvement > 5.0) {
            PlacementSuggestion s;
            s.id = "gap_fill_" + std::to_string(i);
            s.x = c.x; s.y = c.y;
            s.device_type = "agent";
            s.predicted_radius_m = predicted_radius(request.band);
            s.predicted_quality = "Good";
            s.interference_risk = "Low";
            s.coverage_improvement_percent = improvement;
            s.priority = static_cast<int>(improvement / 2);
            s.reason = "Fill coverage gap in underserved area";
            suggestions.push_back(s);
        }
    }
    return suggestions;
}

static std::vector<PlacementSuggestion> improve_weak_zones(const CoverageAnalysis& analysis) {
    std::vector<PlacementSuggestion> suggestions;
    int i = 0;
    for (auto& z : analysis.weak_zones) {
        i++;
        if (z.severity == "high" || z.severity == "critical") {
            PlacementSuggestion s;
            s.id = "weak_zone_" + std::to_string(i);
            s.x = z.center.x; s.y = z.center.y;
            s.device_type = "agent";
            s.predicted_radius_m = 25.0;
            s.predicted_quality = "Good";
            s.interference_risk = "Low";
            s.coverage_improvement_percent = 15.0;
            s.priority = 8;
            s.reason = "Improve " + z.severity + " weak zone (" + std::to_string(z.area_m2) + " m2)";
            suggestions.push_back(s);
        }
    }
    return suggestions;
}

static std::vector<PlacementSuggestion> load_balancing_suggestions() {
    PlacementSuggestion s;
    s.id = "load_balance_1"; s.x = 350; s.y = 200;
    s.device_type = "agent"; s.predicted_radius_m = 20.0;
    s.predicted_quality = "Good"; s.interference_risk = "Medium";
    s.coverage_improvement_percent = 8.0; s.priority = 6;
    s.reason = "Improve load distribution and reduce congestion";
    return { s };
}

static std::vector<PlacementSuggestion> minimal_suggestions() {
    PlacementSuggestion s;
    s.id = "optimization_1"; s.x = 600; s.y = 250;
    s.device_type = "agent"; s.predicted_radius_m = 22.0;
    s.predicted_quality = "Excellent"; s.interference_risk = "Low";
    s.coverage_improvement_percent = 3.0; s.priority = 4;
    s.reason = "Fine-tune coverage for optimal performance";
    return { s };
}

std::vector<PlacementSuggestion> optimize_placement(const OptimizationRequest& request, const std::vector<Device>& devices) {
    CoverageRequest cr;
    cr.band = request.band;
    cr.threshold = request.signal_threshold;
    cr.map_scale = 0.1;
    CoverageAnalysis current = analyze(cr, devices);

    if (current.total_coverage >= request.coverage_target) {
        return minimal_suggestions();
    }

    std::vector<PlacementSuggestion> suggestions;
    auto online = get_online_devices(devices);
    auto gaps = find_coverage_gaps(online, request);
    suggestions.insert(suggestions.end(), gaps.begin(), gaps.end());
    auto weak = improve_weak_zones(current);
    suggestions.insert(suggestions.end(), weak.begin(), weak.end());
    if (suggestions.size() < 3) {
        auto lb = load_balancing_suggestions();
        suggestions.insert(suggestions.end(), lb.begin(), lb.end());
    }

    std::sort(suggestions.begin(), suggestions.end(),
              [](const PlacementSuggestion& a, const PlacementSuggestion& b) { return a.priority > b.priority; });
    if (suggestions.size() > 5) suggestions.resize(5);
    return suggestions;
}

CoverageAnalysis simulate_with_placement(const std::vector<Point3D>& device_positions,
                                            const std::string& band, std::vector<Device> devices) {
    for (auto& pos : device_positions) {
        Device sim;
        std::ostringstream oss;
        oss << "SIM:" << pos.x << ":" << pos.y << ":" << pos.z;
        sim.mac = oss.str();
        sim.status = "Online";
        sim.location.position_3d = pos;
        devices.push_back(sim);
    }
    CoverageRequest req;
    req.band = band; req.threshold = -70; req.map_scale = 0.1; req.include_heatmap = true;
    return analyze(req, devices);
}

} // namespace coverage
