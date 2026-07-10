// ---------------------------------------------------------------------
// VERIFIED against the real rbus.h/rbus_object.h/rbus_property.h/
// rbus_value.h (uploaded from /usr/include/rbus/ on the actual Filogic
// gateway). Two real bugs were found and fixed against the original
// "commonly documented" version of this file:
//
//   1. rbus_discoverComponentName's last param is char*** (plain C-string
//      array) — the rbusComponentName struct type used previously doesn't
//      exist anywhere in this rbus version's headers at all.
//   2. rbus_getExt's properties out-param is rbusProperty_t* (a single
//      opaque handle to the HEAD of a linked list, walked via
//      rbusProperty_GetNext()) — not rbusProperty_t** indexed as an array.
//      This one would have compiled fine (both look like double-pointers
//      at a glance) but returned garbage or crashed at runtime.
//
// Confirmed matching as originally written: rbus_open/rbus_close,
// rbus_discoverComponentDataElements (does take char***), rbusMethod_Invoke,
// rbusObject_Init/SetValue/GetProperties/Release, rbusValue_Init/SetString/
// SetInt32/SetBoolean/Release/GetType/ToString, rbusProperty_GetName/
// GetValue/GetNext/Release.
// ---------------------------------------------------------------------

extern "C" {
#include <rbus.h>
}

#include "rbus_bridge.h"
#include "json_helpers.h"
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace em_rbus {

namespace {
rbusHandle_t g_handle = nullptr;
bool g_connected = false;
std::string g_last_error;
std::mutex g_mu; // guards g_handle/g_connected — REST handlers can be
                  // called from multiple lws service-thread callbacks

const char* kComponentName = "onewifi_em_cli_rbus_explorer";

std::string rbus_error_string(rbusError_t rc) {
    switch (rc) {
        case RBUS_ERROR_SUCCESS: return "success";
        case RBUS_ERROR_BUS_ERROR: return "bus_error";
        case RBUS_ERROR_INVALID_INPUT: return "invalid_input";
        case RBUS_ERROR_NOT_INITIALIZED: return "not_initialized";
        case RBUS_ERROR_OUT_OF_RESOURCES: return "out_of_resources";
        case RBUS_ERROR_DESTINATION_NOT_FOUND: return "destination_not_found";
        case RBUS_ERROR_DESTINATION_NOT_REACHABLE: return "destination_not_reachable";
        case RBUS_ERROR_DESTINATION_RESPONSE_FAILURE: return "destination_response_failure";
        case RBUS_ERROR_INVALID_RESPONSE_FROM_DESTINATION: return "invalid_response_from_destination";
        case RBUS_ERROR_INVALID_OPERATION: return "invalid_operation";
        case RBUS_ERROR_INVALID_EVENT: return "invalid_event";
        case RBUS_ERROR_INVALID_HANDLE: return "invalid_handle";
        case RBUS_ERROR_SESSION_ALREADY_EXIST: return "session_already_exists";
        case RBUS_ERROR_COMPONENT_NAME_DUPLICATE: return "component_name_duplicate";
        case RBUS_ERROR_ELEMENT_NAME_DUPLICATE: return "element_name_duplicate";
        case RBUS_ERROR_ELEMENT_NAME_MISSING: return "element_name_missing";
        case RBUS_ERROR_COMPONENT_DOES_NOT_EXIST: return "component_does_not_exist";
        case RBUS_ERROR_ELEMENT_DOES_NOT_EXIST: return "element_does_not_exist";
        case RBUS_ERROR_ACCESS_NOT_ALLOWED: return "access_not_allowed";
        case RBUS_ERROR_INVALID_CONTEXT: return "invalid_context";
        case RBUS_ERROR_TIMEOUT: return "timeout";
        case RBUS_ERROR_ASYNC_RESPONSE: return "async_response";
        case RBUS_ERROR_INVALID_METHOD: return "invalid_method";
        case RBUS_ERROR_NOSUBSCRIBERS: return "no_subscribers";
        case RBUS_ERROR_SUBSCRIPTION_ALREADY_EXIST: return "subscription_already_exists";
        case RBUS_ERROR_INVALID_NAMESPACE: return "invalid_namespace";
        default: return "rbus_error_" + std::to_string(static_cast<int>(rc));
    }
}

std::string rbus_value_type_name(rbusValueType_t t) {
    switch (t) {
        case RBUS_BOOLEAN: return "boolean";
        case RBUS_CHAR: return "char";
        case RBUS_BYTE: return "byte";
        case RBUS_INT8: return "int8";
        case RBUS_UINT8: return "uint8";
        case RBUS_INT16: return "int16";
        case RBUS_UINT16: return "uint16";
        case RBUS_INT32: return "int32";
        case RBUS_UINT32: return "uint32";
        case RBUS_INT64: return "int64";
        case RBUS_UINT64: return "uint64";
        case RBUS_SINGLE: return "single";
        case RBUS_DOUBLE: return "double";
        case RBUS_DATETIME: return "datetime";
        case RBUS_STRING: return "string";
        case RBUS_BYTES: return "bytes";
        case RBUS_PROPERTY: return "property";
        case RBUS_OBJECT: return "object";
        case RBUS_NONE: return "none";
        default: return "unknown";
    }
}

// Best-effort element classification — see the header comment for why
// this can't be exact from discoverComponentDataElements() alone.
std::string classify_element(const std::string& name) {
    if (name.size() >= 3 && name.compare(name.size() - 3, 3, "{i}") == 0) return "table";
    if (name.find("{i}") != std::string::npos) return "row_property";
    static const std::string suffix = "NumberOfEntries";
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return "count";
    if (name.rfind("Device.", 0) != 0 && name.rfind("Device2.", 0) != 0) return "component_root";
    return "property";
}

void add_value_fields(cJSON* item, rbusValue_t val) {
    if (!val) {
        add_str(item, "type", "none");
        add_str(item, "value", "");
        return;
    }
    rbusValueType_t type = rbusValue_GetType(val);
    add_str(item, "type", rbus_value_type_name(type));

    if (type == RBUS_STRING) {
        // FIXED: rbusValue_ToString's fixed-size caller buffer silently
        // truncated large string values (confirmed on the real gateway —
        // Device.WiFi.DataElements.Network.Topology is several KB, and a
        // 1024-byte buffer cut it off mid-JSON-object, which then failed
        // to parse downstream and silently produced empty results instead
        // of an error, exactly the "I don't see the real data" symptom).
        // rbusValue_GetString returns a direct pointer + length with no
        // size limit — the correct extraction path for string-typed
        // values instead of the generic display-oriented ToString.
        int len = 0;
        const char* s = rbusValue_GetString(val, &len);
        add_str(item, "value", s ? std::string(s, static_cast<size_t>(len)) : "");
        return;
    }

    // Non-string scalars (bool/int/etc.) are always short — the fixed
    // buffer is fine here.
    char buf[256] = {0};
    rbusValue_ToString(val, buf, sizeof(buf) - 1);
    add_str(item, "value", buf);
}

// Builds an rbusValue_t from one cJSON leaf, for method invoke params.
// Caller owns/releases the returned value.
rbusValue_t value_from_cjson(cJSON* field) {
    rbusValue_t v = nullptr;
    rbusValue_Init(&v);
    if (cJSON_IsString(field)) {
        rbusValue_SetString(v, field->valuestring);
    } else if (cJSON_IsBool(field)) {
        rbusValue_SetBoolean(v, cJSON_IsTrue(field) ? true : false);
    } else if (cJSON_IsNumber(field)) {
        double d = field->valuedouble;
        if (d == static_cast<double>(static_cast<int>(d))) {
            rbusValue_SetInt32(v, static_cast<int>(d));
        } else {
            rbusValue_SetDouble(v, d);
        }
    } else {
        // Object/array/null param value — best effort, stringify it
        // rather than dropping it silently.
        char* s = cJSON_PrintUnformatted(field);
        rbusValue_SetString(v, s ? s : "");
        if (s) free(s);
    }
    return v;
}

} // namespace

void init() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_connected) return;
    rbusError_t rc = rbus_open(&g_handle, kComponentName);
    if (rc != RBUS_ERROR_SUCCESS) {
        g_connected = false;
        g_last_error = "rbus_open failed: " + rbus_error_string(rc);
        g_handle = nullptr;
        return;
    }
    g_connected = true;
    g_last_error.clear();
}

void shutdown() {
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_connected && g_handle) {
        rbus_close(g_handle);
    }
    g_handle = nullptr;
    g_connected = false;
}

bool is_connected() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_connected;
}

std::string last_error() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_last_error;
}

cJSON* discover_elements(const std::string& component) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "component", component);

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_connected) {
        add_str(root.get(), "error", g_last_error.empty() ? "rbus not connected" : g_last_error);
        return root.release();
    }

    int numElements = 0;
    char** elementNames = nullptr;
    // nextLevel=false: return the component's full registered element
    // list (schema/template names), matching the on-device
    // `discelements WifiCtrl` output that returned all 651 elements in
    // one call, not just immediate children.
    rbusError_t rc = rbus_discoverComponentDataElements(
        g_handle, component.c_str(), false, &numElements, &elementNames);

    if (rc != RBUS_ERROR_SUCCESS) {
        add_str(root.get(), "error", rbus_error_string(rc));
        cJSON_AddNumberToObject(root.get(), "rbus_error_code", static_cast<double>(rc));
        return root.release();
    }

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < numElements; ++i) {
        std::string name = (elementNames && elementNames[i]) ? elementNames[i] : "";
        cJSON* item = cJSON_CreateObject();
        add_str(item, "name", name);
        add_str(item, "kind", classify_element(name));
        cJSON_AddItemToArray(arr, item);
        if (elementNames && elementNames[i]) free(elementNames[i]);
    }
    if (elementNames) free(elementNames);

    cJSON_AddItemToObject(root.get(), "elements", arr);
    cJSON_AddNumberToObject(root.get(), "count", numElements);
    return root.release();
}

cJSON* discover_components_for_path(const std::string& path) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "path", path);

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_connected) {
        add_str(root.get(), "error", g_last_error.empty() ? "rbus not connected" : g_last_error);
        return root.release();
    }

    const char* elementNames[1] = { path.c_str() };
    int numComponents = 0;
    // CORRECTED against the real rbus.h: rbus_discoverComponentName's last
    // param is char*** (array of C-string component names), not
    // rbusComponentName** — that struct type doesn't exist anywhere in
    // this rbus version's headers. It was an assumption from generic
    // rbus documentation that didn't match this device's actual API.
    char** components = nullptr;
    rbusError_t rc = rbus_discoverComponentName(
        g_handle, 1, elementNames, &numComponents, &components);

    if (rc != RBUS_ERROR_SUCCESS) {
        add_str(root.get(), "error", rbus_error_string(rc));
        cJSON_AddNumberToObject(root.get(), "rbus_error_code", static_cast<double>(rc));
        return root.release();
    }

    cJSON* arr = cJSON_CreateArray();
    for (int i = 0; i < numComponents; ++i) {
        if (components && components[i]) {
            cJSON_AddItemToArray(arr, cJSON_CreateString(components[i]));
            free(components[i]);
        }
    }
    if (components) free(components);

    cJSON_AddItemToObject(root.get(), "components", arr);
    return root.release();
}

cJSON* get_values(const std::vector<std::string>& paths) {
    CJsonPtr root(cJSON_CreateObject());
    cJSON* arr = cJSON_CreateArray();

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_connected) {
        add_str(root.get(), "error", g_last_error.empty() ? "rbus not connected" : g_last_error);
        cJSON_AddItemToObject(root.get(), "results", arr);
        return root.release();
    }

    std::vector<const char*> cpaths;
    cpaths.reserve(paths.size());
    for (auto& p : paths) cpaths.push_back(p.c_str());

    int numProps = 0;
    // CORRECTED against the real rbus.h: rbus_getExt's output param is
    // `rbusProperty_t* properties` — a single rbusProperty_t (itself an
    // opaque pointer, struct _rbusProperty*) that on return points to the
    // HEAD of a linked list (rbusProperty_GetNext() chains through it),
    // not an array you index with properties[i]. The array-indexing
    // version would have compiled (rbusProperty_t* both ways look similar)
    // but silently returned garbage/crashed at runtime — the kind of bug
    // that's easy to miss without the real header in hand.
    rbusProperty_t properties = nullptr;
    rbusError_t rc = rbus_getExt(g_handle, static_cast<int>(cpaths.size()),
                                  cpaths.data(), &numProps, &properties);

    if (rc != RBUS_ERROR_SUCCESS) {
        // Whole-batch failure (e.g. destination unreachable) — report the
        // same error against every requested path so the UI can still
        // render a row per request instead of just a blank result.
        for (auto& p : paths) {
            cJSON* item = cJSON_CreateObject();
            add_str(item, "name", p);
            add_str(item, "error", rbus_error_string(rc));
            cJSON_AddItemToArray(arr, item);
        }
        cJSON_AddItemToObject(root.get(), "results", arr);
        return root.release();
    }

    rbusProperty_t prop = properties;
    for (int i = 0; i < numProps && prop != nullptr; ++i) {
        cJSON* item = cJSON_CreateObject();
        const char* pname = rbusProperty_GetName(prop);
        add_str(item, "name", pname ? pname : "");
        add_value_fields(item, rbusProperty_GetValue(prop));
        cJSON_AddItemToArray(arr, item);
        prop = rbusProperty_GetNext(prop);
    }
    // Releasing the head releases the whole chain (same ownership model
    // as rbusObject_Release over a property list built with rbusObject_Init
    // — the bus layer allocated this as one chain, so one release call
    // covers it, matching the convention used elsewhere in this file for
    // rbusMethod_Invoke's outParams).
    if (properties) rbusProperty_Release(properties);

    cJSON_AddItemToObject(root.get(), "results", arr);
    return root.release();
}

cJSON* invoke_method(const std::string& method_name, cJSON* params) {
    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "method", method_name);

    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_connected) {
        add_str(root.get(), "error", g_last_error.empty() ? "rbus not connected" : g_last_error);
        return root.release();
    }

    rbusObject_t inParams = nullptr;
    rbusObject_Init(&inParams, nullptr);
    if (params && cJSON_IsObject(params)) {
        cJSON* field = nullptr;
        cJSON_ArrayForEach(field, params) {
            rbusValue_t v = value_from_cjson(field);
            rbusObject_SetValue(inParams, field->string, v);
            rbusValue_Release(v);
        }
    }

    rbusObject_t outParams = nullptr;
    rbusError_t rc = rbusMethod_Invoke(g_handle, method_name.c_str(), inParams, &outParams);
    rbusObject_Release(inParams);

    if (rc != RBUS_ERROR_SUCCESS) {
        add_str(root.get(), "error", rbus_error_string(rc));
        cJSON_AddNumberToObject(root.get(), "rbus_error_code", static_cast<double>(rc));
        return root.release();
    }

    cJSON* outArr = cJSON_CreateArray();
    if (outParams) {
        for (rbusProperty_t prop = rbusObject_GetProperties(outParams); prop != nullptr;
             prop = rbusProperty_GetNext(prop)) {
            cJSON* item = cJSON_CreateObject();
            const char* pname = rbusProperty_GetName(prop);
            add_str(item, "name", pname ? pname : "");
            add_value_fields(item, rbusProperty_GetValue(prop));
            cJSON_AddItemToArray(outArr, item);
        }
        rbusObject_Release(outParams);
    }

    cJSON_AddItemToObject(root.get(), "outParams", outArr);
    add_str(root.get(), "status", "success");
    return root.release();
}

} // namespace em_rbus
