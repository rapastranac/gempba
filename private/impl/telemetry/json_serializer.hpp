#ifndef GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP
#define GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP

#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>

#include <string>

namespace gempba::telemetry {

    void serialize_worker_frame(std::string& p_out, const worker_frame& p_frame);
    void serialize_node_frame(std::string& p_out, const node_frame& p_frame);
    void serialize_topology(std::string& p_out, const topology_snapshot& p_topology);

    struct broadcast_payload {
        std::uint64_t m_now_ms;
        std::uint64_t m_elapsed_seconds;
        const topology_snapshot* m_topology;
        const std::vector<worker_frame>* m_workers;
        const std::vector<node_frame>* m_nodes;
    };

    void serialize_broadcast(std::string& p_out, const broadcast_payload& p_payload);

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP
