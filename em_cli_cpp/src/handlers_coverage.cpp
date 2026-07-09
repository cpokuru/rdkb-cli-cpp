#include "handlers.h"
#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "coverage.h"
#include <thread>
#include <algorithm>

MHD_Result handle_get_coverage_analysis(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    {
        std::shared_lock lock(state.coverage_mu);
        if (state.has_current_coverage) {
            CJsonPtr root(coverage_analysis_to_json(state.current_coverage));
            return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
        }
    }
    // Not available yet — kick off analysis in the background (matches the
    // Go handler's `go analyzeCurrentCoverage()`) and return a basic
    // placeholder response immediately, same shape as the Go fallback.
    std::thread([]() {
        auto& state = AppState::instance();
        CoverageRequest req; req.band = "2.4ghz"; req.threshold = -70; req.map_scale = 0.1;
        std::vector<Device> devices_copy;
        { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }
        CoverageAnalysis analysis = coverage::analyze(req, devices_copy);
        std::unique_lock lock(state.coverage_mu);
        state.current_coverage = analysis;
        state.has_current_coverage = true;
    }).detach();

    CoverageAnalysis placeholder;
    placeholder.total_coverage = 85.0;
    placeholder.excellent_coverage = 60.0;
    placeholder.weak_areas = 15.5;
    placeholder.dead_zones = 5.2;
    placeholder.interference_level = "Low";
    placeholder.analyzed_at = now_epoch();
    CJsonPtr root(coverage_analysis_to_json(placeholder));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_coverage_analyze(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    CoverageRequest req = coverage_request_from_json(parsed ? parsed.get() : nullptr);

    auto& state = AppState::instance();
    std::vector<Device> devices_copy;
    { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

    CoverageAnalysis analysis = coverage::analyze(req, devices_copy);
    {
        std::unique_lock lock(state.coverage_mu);
        if (!state.has_current_coverage || req.band == "2.4ghz") {
            state.current_coverage = analysis;
            state.has_current_coverage = true;
        }
    }

    CJsonPtr root(coverage_analysis_to_json(analysis));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_coverage_optimize(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    OptimizationRequest req = optimization_request_from_json(parsed ? parsed.get() : nullptr);

    auto& state = AppState::instance();
    std::vector<Device> devices_copy;
    { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

    auto suggestions = coverage::optimize_placement(req, devices_copy);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    cJSON* arr = cJSON_CreateArray();
    for (auto& s : suggestions) cJSON_AddItemToArray(arr, placement_suggestion_to_json(s));
    cJSON_AddItemToObject(root.get(), "suggestions", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Floor plans (in-memory CRUD, matches floorPlans map in main.go) =====

MHD_Result handle_get_floorplans(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateObject();
    for (auto& f : state.floor_plans) cJSON_AddItemToObject(arr, f.id.c_str(), floor_plan_to_json(f));
    cJSON_AddItemToObject(root.get(), "floor_plans", arr);
    cJSON_AddNumberToObject(root.get(), "total", (double)state.floor_plans.size());
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_floorplans(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    FloorPlan f;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "id")) if (cJSON_IsString(v)) f.id = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "name")) if (cJSON_IsString(v)) f.name = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "url")) if (cJSON_IsString(v)) f.url = v->valuestring;
    if (f.name.empty()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Floor plan name is required");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    if (f.id.empty()) f.id = "floorplan_" + std::to_string(static_cast<long long>(now_epoch() * 1e9));
    f.created_at = f.updated_at = now_epoch();

    auto& state = AppState::instance();
    { std::unique_lock lock(state.coverage_mu); state.floor_plans.push_back(f); }

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Floor plan uploaded successfully");
    cJSON_AddItemToObject(root.get(), "plan", floor_plan_to_json(f));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_floorplan(struct MHD_Connection* connection, const std::string& id) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    if (auto* f = state.find_floor_plan(id)) {
        CJsonPtr root(floor_plan_to_json(*f));
        return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
    }
    CJsonPtr err(cJSON_CreateObject());
    add_str(err.get(), "error", "Floor plan not found");
    return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
}

MHD_Result handle_put_floorplan(struct MHD_Connection* connection, const std::string& id, const std::string& body) {
    auto& state = AppState::instance();
    std::unique_lock lock(state.coverage_mu);
    auto* existing = state.find_floor_plan(id);
    if (!existing) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Floor plan not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    double created_at = existing->created_at;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "name")) if (cJSON_IsString(v)) existing->name = v->valuestring;
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "url")) if (cJSON_IsString(v)) existing->url = v->valuestring;
    existing->created_at = created_at;
    existing->updated_at = now_epoch();

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Floor plan updated successfully");
    cJSON_AddItemToObject(root.get(), "plan", floor_plan_to_json(*existing));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_delete_floorplan(struct MHD_Connection* connection, const std::string& id) {
    auto& state = AppState::instance();
    std::unique_lock lock(state.coverage_mu);
    auto it = std::find_if(state.floor_plans.begin(), state.floor_plans.end(),
                            [&](const FloorPlan& f) { return f.id == id; });
    if (it == state.floor_plans.end()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Floor plan not found");
        return send_json(connection, MHD_HTTP_NOT_FOUND, to_json_string(err.get()));
    }
    state.floor_plans.erase(it);
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "success", true);
    add_str(root.get(), "message", "Floor plan deleted successfully");
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Heatmap =====

MHD_Result handle_get_heatmap(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    if (!state.has_current_coverage) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Coverage analysis in progress");
        return send_json(connection, 503, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON* map = cJSON_CreateArray();
    for (auto& row : state.current_coverage.coverage_map) {
        cJSON* r = cJSON_CreateArray();
        for (auto& p : row) cJSON_AddItemToArray(r, signal_point_to_json(p));
        cJSON_AddItemToArray(map, r);
    }
    cJSON_AddItemToObject(root.get(), "heatmap", map);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(state.current_coverage.analyzed_at));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_band_heatmap(struct MHD_Connection* connection, const std::string& band) {
    auto& state = AppState::instance();
    std::vector<Device> devices_copy;
    { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

    CoverageRequest req; req.band = band.empty() ? "2.4ghz" : band; req.threshold = -70; req.map_scale = 0.1; req.include_heatmap = true;
    CoverageAnalysis analysis = coverage::analyze(req, devices_copy);

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "band", req.band);
    cJSON* map = cJSON_CreateArray();
    for (auto& row : analysis.coverage_map) {
        cJSON* r = cJSON_CreateArray();
        for (auto& p : row) cJSON_AddItemToArray(r, signal_point_to_json(p));
        cJSON_AddItemToArray(map, r);
    }
    cJSON_AddItemToObject(root.get(), "heatmap", map);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(analysis.analyzed_at));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

// ===== Placement simulation / prediction =====

MHD_Result handle_post_simulate_placement(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    std::string band = "2.4ghz";
    if (cJSON* v = cJSON_GetObjectItem(parsed.get(), "band")) if (cJSON_IsString(v)) band = v->valuestring;
    std::vector<Point3D> positions;
    if (cJSON* devs = cJSON_GetObjectItem(parsed.get(), "devices")) {
        int n = cJSON_GetArraySize(devs);
        for (int i = 0; i < n; i++) {
            cJSON* item = cJSON_GetArrayItem(devs, i);
            Point3D p;
            if (cJSON* v = cJSON_GetObjectItem(item, "x")) p.x = v->valuedouble;
            if (cJSON* v = cJSON_GetObjectItem(item, "y")) p.y = v->valuedouble;
            if (cJSON* v = cJSON_GetObjectItem(item, "z")) p.z = v->valuedouble;
            positions.push_back(p);
        }
    }

    auto& state = AppState::instance();
    std::vector<Device> devices_copy;
    { std::shared_lock lock(state.devices_mu); devices_copy = state.devices; }

    CoverageAnalysis coverage_result = coverage::simulate_with_placement(positions, band, devices_copy);

    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddItemToObject(root.get(), "coverage", coverage_analysis_to_json(coverage_result));
    add_str(root.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_predict_placement(struct MHD_Connection* connection, const std::string& body) {
    return handle_post_coverage_optimize(connection, body); // same computation, mirrors Go's near-identical handlers
}

// ===== Weak zones / dead spots / reports =====

MHD_Result handle_get_weak_zones(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    if (!state.has_current_coverage) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "No coverage analysis available");
        return send_json(connection, 503, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& z : state.current_coverage.weak_zones) cJSON_AddItemToArray(arr, weak_zone_to_json(z));
    cJSON_AddItemToObject(root.get(), "weak_zones", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(state.current_coverage.analyzed_at));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_dead_spots(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    if (!state.has_current_coverage) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "No coverage analysis available");
        return send_json(connection, 503, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();
    for (auto& z : state.current_coverage.weak_zones)
        if (z.severity == "critical") cJSON_AddItemToArray(arr, weak_zone_to_json(z));
    cJSON_AddItemToObject(root.get(), "dead_spots", arr);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(state.current_coverage.analyzed_at));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_coverage_report(struct MHD_Connection* connection) {
    auto& state = AppState::instance();
    std::shared_lock lock(state.coverage_mu);
    if (!state.has_current_coverage) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "No coverage analysis available");
        return send_json(connection, 503, to_json_string(err.get()));
    }
    CJsonPtr root(cJSON_CreateObject());
    cJSON_AddNumberToObject(root.get(), "total_coverage", state.current_coverage.total_coverage);
    cJSON_AddNumberToObject(root.get(), "excellent_coverage", state.current_coverage.excellent_coverage);
    cJSON* wz = cJSON_CreateArray();
    for (auto& z : state.current_coverage.weak_zones) cJSON_AddItemToArray(wz, weak_zone_to_json(z));
    cJSON_AddItemToObject(root.get(), "weak_zones", wz);
    cJSON* ps = cJSON_CreateArray();
    for (auto& s : state.current_coverage.placement_suggestions) cJSON_AddItemToArray(ps, placement_suggestion_to_json(s));
    cJSON_AddItemToObject(root.get(), "placement_suggestions", ps);
    add_str(root.get(), "timestamp", epoch_to_rfc3339(state.current_coverage.analyzed_at));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_coverage_report_pdf(struct MHD_Connection* connection) {
    // Matches the Go version's placeholder — real PDF generation would use
    // the pdf skill / a PDF library; left as-is since the original was also
    // just a placeholder byte stream, not real PDF generation.
    static const std::string placeholder = "PDF Generation Placeholder";
    struct MHD_Response* response = MHD_create_response_from_buffer(
        placeholder.size(), (void*)placeholder.c_str(), MHD_RESPMEM_MUST_COPY);
    MHD_add_response_header(response, "Content-Type", "application/pdf");
    MHD_add_response_header(response, "Content-Disposition", "attachment; filename=coverage_report.pdf");
    MHD_Result ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
    MHD_destroy_response(response);
    return ret;
}
