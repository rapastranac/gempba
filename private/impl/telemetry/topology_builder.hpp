#ifndef GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP

#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/topology.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace gempba::telemetry {

    /**
     * @brief Build the static topology snapshot for the current runtime.
     *
     * Called once by @ref telemetry_hub::on_runtime_ready. The MT-only path
     * synthesizes a single-node snapshot; the multi-process path gathers each
     * host's layout from its sentinel and merges them into one snapshot.
     */
    topology_snapshot build_topology_snapshot(runtime_mode p_mode, std::uint32_t p_worker_id, std::uint32_t p_world_size);

    namespace detail {

        /**
         * @brief Serialize a node's static topology (hostname, core counts,
         * memory, sockets with their full CPU-ID lists) into a portable byte
         * buffer, appended to @p p_out.
         *
         * Used for the variable-length gather that lets the center carry every
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
