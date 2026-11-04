#ifndef GEMPBA_TELEMETRY_TOPOLOGY_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_HPP

#include <gempba/core/rank_role.hpp>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/runtime_mode.hpp>

#include <cstdint>
#include <string>
#include <utility>
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

    /** @brief Who recorded a @ref run_marker. */
    enum class marker_source : std::uint8_t {
        CODE,
        DASHBOARD,
    };

    /** @brief A labelled moment of the run; see @ref telemetry_hub::mark_run. */
    struct run_marker {
        /// Milliseconds since telemetry came up, the clock the broadcast's @c elapsed_seconds counts.
        std::uint64_t m_elapsed_ms = 0;
        std::string m_label;
        marker_source m_source = marker_source::CODE;
    };

    /**
     * @brief How a run is set up, as the aggregator describes it in each broadcast.
     *
     * Each rank's role is the one its scheduler gives it; a multithreading run's only rank is a worker.
     */
    struct run_description {
        runtime_mode m_mode = runtime_mode::MT_ONLY;
        /// Scheduler name, e.g. "centralized"; empty when there is no scheduler.
        std::string m_scheduler;
        /// Load balancer name, "quasi_horizontal" or "work_stealing"; empty until one is created.
        std::string m_load_balancer;
        std::uint32_t m_world_size = 1;
        /// Each rank's role, indexed by worker id, as its scheduler reports it; a rank past the end is a worker.
        std::vector<rank_role> m_roles;
        /// Largest thread pool the workers report in their frames; 0 before any frame arrives.
        std::uint32_t m_threads_per_process = 0;
        std::string m_gempba_version;
        std::vector<std::pair<std::string, std::string>> m_annotations;
        /// The aggregator's markers, in the order they were recorded.
        std::vector<run_marker> m_markers;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_HPP
