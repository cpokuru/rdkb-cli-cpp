// REST handlers for the rbus explorer feature. Thin wrappers around
// rbus_bridge.{h,cpp} — all the actual librbus calls live there; this
// file only does request parsing/validation and JSON response shaping,
// matching the pattern every other handlers_*.cpp file in this project
// already follows.
#include "handlers.h"
#include "http_server.h"
#include "json_helpers.h"
#include "rbus_bridge.h"
#include <cstring>
#include <vector>

MHD_Result handle_get_rbus_status(struct MHD_Connection* connection) {
    CJsonPtr root(cJSON_CreateObject());
    bool connected = em_rbus::is_connected();
    cJSON_AddBoolToObject(root.get(), "connected", connected);
    add_str(root.get(), "component", "onewifi_em_cli_rbus_explorer");
    if (!connected) add_str(root.get(), "error", em_rbus::last_error());
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_rbus_components(struct MHD_Connection* connection, const std::string& path) {
    if (path.empty()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Missing required query parameter: path");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    CJsonPtr root(em_rbus::discover_components_for_path(path));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_get_rbus_elements(struct MHD_Connection* connection, const std::string& component) {
    if (component.empty()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Missing required query parameter: component");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    CJsonPtr root(em_rbus::discover_elements(component));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_rbus_get(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed || !cJSON_IsObject(parsed.get())) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON body, expected {\"paths\": [\"...\"]}");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    cJSON* paths_arr = cJSON_GetObjectItem(parsed.get(), "paths");
    if (!paths_arr || !cJSON_IsArray(paths_arr)) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "'paths' must be a JSON array of strings");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    std::vector<std::string> paths;
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, paths_arr) {
        if (cJSON_IsString(item) && item->valuestring) paths.push_back(item->valuestring);
    }
    if (paths.empty()) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "'paths' array was empty");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    CJsonPtr root(em_rbus::get_values(paths));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_rbus_method_invoke(struct MHD_Connection* connection, const std::string& body) {
    CJsonPtr parsed(cJSON_Parse(body.c_str()));
    if (!parsed || !cJSON_IsObject(parsed.get())) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "Invalid JSON body, expected {\"method\": \"...\", \"params\": {...}}");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    cJSON* method_item = cJSON_GetObjectItem(parsed.get(), "method");
    if (!method_item || !cJSON_IsString(method_item) || !method_item->valuestring ||
        strlen(method_item->valuestring) == 0) {
        CJsonPtr err(cJSON_CreateObject());
        add_str(err.get(), "error", "'method' must be a non-empty string");
        return send_json(connection, MHD_HTTP_BAD_REQUEST, to_json_string(err.get()));
    }
    cJSON* params = cJSON_GetObjectItem(parsed.get(), "params"); // may be null -> no-arg method
    CJsonPtr root(em_rbus::invoke_method(method_item->valuestring, params));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}
