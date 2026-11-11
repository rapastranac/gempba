#ifndef GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP

#include <gempba/telemetry/topology.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gempba::telemetry {

    class telemetry_transport;

    /**
     * @brief Build the static topology snapshot for this run.
     *
     * Called once by @ref telemetry_hub::on_runtime_ready. A run of one rank describes its own host. Otherwise every rank
     * gathers the others' identities through @p p_transport, and each host's sentinel contributes that host's layout.
     * Each rank's identity carries @p p_role, its own scheduler role.
     */
    topology_snapshot build_topology_snapshot(telemetry_transport& p_transport, std::uint32_t p_rank, std::uint32_t p_world_size, rank_role p_role = rank_role::WORKER);

    namespace detail {

        /**
         * @brief Serialize a node's static topology (hostname, core counts,
         * memory, sockets with their full CPU-ID lists) into a portable byte
         * buffer, appended to @p p_out.
         *
         * Used for the variable-length gather that lets the aggregator carry every
         * host's layout, not just rank 0's. Unlike a fixed-size record this never
         * truncates a socket's CPU IDs, so wide sockets (e.g. 96-core EPYC) are
         * represented exactly. Encoding is little-endian and self-describing.
         */
        void serialize_node_topology(std::vector<std::uint8_t>& p_out, const topology_node& p_node);

        /**
         * @brief Inverse of @ref serialize_node_topology: fill @p p_node's
         * hardware fields (hostname / sockets / core counts / memory) from a
         * buffer. Returns false if the buffer is truncated or malformed.
         */
        bool deserialize_node_topology(topology_node& p_node, const std::uint8_t* p_data, std::size_t p_size);

    } // namespace detail

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
