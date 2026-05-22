#ifndef GEMPBA_TELEMETRY_TOPOLOGY_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gempba::telemetry {

    /**
     * @brief One CPU socket inside a @ref topology_node.
     *
     * Per-socket fields are populated by the hardware topology probe; counts
     * are zero when the probe is unavailable.
     */
    struct topology_socket {
        std::uint8_t m_socket_id = 0;
        std::string m_name;
        /// Physical core count on this socket; 0 when the topology probe is unavailable.
        std::uint16_t m_physical_cores = 0;
        std::uint16_t m_logical_cores = 0;
        /// Logical CPU IDs that belong to this socket.
        std::vector<std::uint32_t> m_cpu_ids;
    };

    /**
     * @brief One host in the run, with the workers it owns and its hardware layout.
     *
     * One sentinel worker per node emits the @ref node_frame stream for the host.
     */
    struct topology_node {
        std::string m_hostname;
        std::vector<std::uint32_t> m_worker_ids;
        std::uint32_t m_sentinel_worker_id = 0;
        std::vector<topology_socket> m_sockets;
        /// Total physical cores on this host; 0 when the topology probe is unavailable.
        std::uint16_t m_total_physical_cores = 0;
        std::uint16_t m_total_logical_cores = 0;
        std::uint64_t m_mem_total_bytes = 0;
    };

    /**
     * @brief Static snapshot of the full run topology: every host and every worker.
     *
     * Captured once at hub startup. Frames carry numeric worker_ids; this
     * snapshot is the mapping back to host + hardware identity.
     */
    struct topology_snapshot {
        std::vector<topology_node> m_nodes;
        std::vector<worker_identity> m_identities;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_HPP
