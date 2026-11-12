#ifndef GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP
#define GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP

#include <gempba/core/stop.hpp>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gempba::telemetry {

    /// Hand-rolled JSON encoders for the dashboard wire format. Append to @p p_out.
    void serialize_rank_frame(std::string& p_out, const rank_frame& p_frame);
    void serialize_node_frame(std::string& p_out, const node_frame& p_frame);
    /// With @p p_run, also emits each identity's role and the run object.
    void serialize_topology(std::string& p_out, const topology_snapshot& p_topology, const run_description* p_run = nullptr);

    /// One dashboard broadcast tick: topology + latest rank / node frames.
    struct broadcast_payload {
        std::string_view m_run_id;
        std::uint64_t m_now_ms;
        std::uint64_t m_elapsed_seconds;
        const topology_snapshot* m_topology;
        const run_description* m_run;
        const std::vector<rank_frame>* m_rank_frames;
        const std::vector<node_frame>* m_nodes;
        bool m_paused;
        /// Whether this is the line the aggregator sends at shutdown, the last before it closes the connection.
        bool m_final;
        /// How the run ended, sent as @c end on the final line when known.
        std::optional<run_outcome> m_end;
    };

    /// Serialize one @ref broadcast_payload to a single JSON line.
    void serialize_broadcast(std::string& p_out, const broadcast_payload& p_payload);

    /// Serialize the line a paused aggregator broadcasts: the header of @ref serialize_broadcast with @c "paused":true and no topology or frames.
    void serialize_paused_broadcast(std::string& p_out, const broadcast_payload& p_payload);

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_JSON_SERIALIZER_HPP
