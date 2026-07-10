#include "handlers.h"
#include "http_server.h"
#include "app_state.h"
#include "json_helpers.h"
#include "rbus_datamodel_bridge.h"
#include "ws_server.h"

MHD_Result handle_get_topology(struct MHD_Connection* connection) {
    auto result = em_rbus::get_topology();

    CJsonPtr root(cJSON_CreateObject());
    cJSON* nodes = cJSON_CreateArray();
    for (auto& n : result.nodes) cJSON_AddItemToArray(nodes, topo_node_to_json(n));
    cJSON_AddItemToObject(root.get(), "nodes", nodes);
    cJSON* edges = cJSON_CreateArray();
    for (auto& e : result.edges) cJSON_AddItemToArray(edges, topo_edge_to_json(e));
    cJSON_AddItemToObject(root.get(), "edges", edges);

    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}

MHD_Result handle_post_topology_optimize(struct MHD_Connection* connection) {
    auto& state = AppState::instance();

    MeshTopology topo;
    {
        std::shared_lock lock(state.devices_mu);
        topo.nodes = static_cast<int>(state.devices.size());
    }
    topo.mesh_id = "EasyMesh-R6-Network-001";
    topo.controller_mac = "AA:BB:CC:00:00:01";
    topo.protocol = "IEEE 1905.1 + Multi-AP R6";
    topo.version = "R6";
    topo.performance.average_throughput_mbps = 847.5;
    topo.performance.average_latency_ms = 3.2;
    {
        std::shared_lock lock(state.clients_mu);
        topo.performance.total_clients = static_cast<int>(state.clients.size());
    }
    topo.last_updated = now_epoch();

    {
        std::lock_guard<std::mutex> lock(state.topology_mu);
        state.topology = topo;
    }

    CJsonPtr bcast(cJSON_CreateObject());
    add_str(bcast.get(), "type", "topology_optimized");
    cJSON_AddItemToObject(bcast.get(), "topology", mesh_topology_to_json(topo));
    add_str(bcast.get(), "timestamp", epoch_to_rfc3339(now_epoch()));
    if (auto* ws = global_ws_server()) ws->broadcast(to_json_string(bcast.get()));

    CJsonPtr root(cJSON_CreateObject());
    add_str(root.get(), "status", "success");
    add_str(root.get(), "message", "Topology optimization completed");
    cJSON_AddItemToObject(root.get(), "topology", mesh_topology_to_json(topo));
    return send_json(connection, MHD_HTTP_OK, to_json_string(root.get()));
}
