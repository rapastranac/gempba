#ifndef GEMPBA_TELEMETRY_FRAMES_HPP
#define GEMPBA_TELEMETRY_FRAMES_HPP

#include <cstdint>

namespace gempba::telemetry {

    /// Hard upper bound on worker_id values; sizes the worker-fanout arrays.
    inline constexpr unsigned MAX_WORKERS = 1024;
    /// Hard upper bound on per-host socket count; sizes node_frame::m_sockets.
    inline constexpr unsigned MAX_SOCKETS = 8;

    /// Hard upper bound on a node's logical CPU count, and the derived 64-bit word
    /// count for worker_identity's allowed-CPU bitmap. Covers the widest current
    /// servers (e.g. dual 192-core SMT) with headroom.
    inline constexpr unsigned MAX_LOGICAL_CPUS = 1024;
    inline constexpr unsigned CPU_MASK_WORDS = MAX_LOGICAL_CPUS / 64;

    /// Wire-format version stamped into every frame. Bump on any layout change.
    inline constexpr std::uint16_t TELEMETRY_SCHEMA_VERSION = 1;

    /**
     * @brief Static, one-shot description of a worker.
     *
     * Published once per worker at startup. Joins a worker_id to a host / pid
     * plus its CPU-affinity mask so the dashboard can place it in the topology.
     */
    struct worker_identity {
        std::uint16_t m_version;
        std::uint32_t m_worker_id;
        char m_hostname[64];
        std::uint32_t m_pid;
        /// Socket the worker's affinity places it on (0 when affinity is unknown).
        std::uint8_t m_primary_socket;
        /// Bitmap of the logical CPUs this worker may run on, one bit per CPU
        /// across @ref MAX_LOGICAL_CPUS: word @c w bit @c b means CPU @c w*64+b.
        /// Wide enough to represent every CPU on big nodes, not just the low 64.
        std::uint64_t m_allowed_cpu_mask[CPU_MASK_WORDS];
    };

    /// One outbound edge in @ref worker_frame::m_edges_out.
    struct edge_counter {
        std::uint64_t m_bytes;
        std::uint64_t m_count;
    };

    /**
     * @brief Periodic per-worker telemetry sample.
     *
     * Emitted on the worker-interval cadence. Counters are cumulative since
     * worker startup; consumers compute deltas if they want rate-of-change.
     */
    struct worker_frame {
        std::uint16_t m_version;
        std::uint32_t m_worker_id;
        std::uint32_t m_seq_no;
        std::uint64_t m_worker_local_ms;

        /// Tasks dispatched to the worker's local thread pool.
        /// Mutually exclusive with @ref m_tasks_sent_total: each task event
        /// is either local-dispatch or a remote send.
        std::uint64_t m_tasks_local_total;
        /// Tasks sent to a remote worker.
        std::uint64_t m_tasks_sent_total;
        /// Tasks received from a remote worker.
        std::uint64_t m_tasks_recv_total;
        /// Tasks currently executing on the worker's thread pool.
        std::uint32_t m_tasks_running;
        /// Outstanding requests in the scheduler queue.
        std::uint32_t m_scheduler_pending_count;
        /// Average microseconds a pool thread has been idle since startup
        /// (cumulative pool idle time / pool size).
        std::uint64_t m_idle_microseconds_per_worker;

        float m_process_cpu_pct;
        std::uint64_t m_process_rss_bytes;
        std::uint32_t m_process_threads;

        /// Per-destination outbound traffic. Index = destination worker_id.
        edge_counter m_edges_out[MAX_WORKERS];
    };

    /// Per-socket CPU + memory stats inside @ref node_frame::m_sockets.
    struct socket_stats {
        std::uint8_t m_socket_id;
        float m_cpu_pct;
        std::uint64_t m_mem_total_bytes;
        std::uint64_t m_mem_used_bytes;
    };

    /// Host-level network counters, delta-able by the consumer.
    struct net_stats {
        std::uint64_t m_bytes_in;
        std::uint64_t m_bytes_out;
        std::uint64_t m_packets_in;
        std::uint64_t m_packets_out;
    };

    /// Host-level disk counters, delta-able by the consumer.
    struct disk_stats {
        std::uint64_t m_read_bytes;
        std::uint64_t m_write_bytes;
    };

    /**
     * @brief Periodic per-host telemetry sample.
     *
     * Emitted by the host's sentinel worker on the node-interval cadence.
     * Aggregates host-wide signals (CPU, memory, net, disk) that are not
     * meaningful per-worker.
     */
    struct node_frame {
        std::uint16_t m_version;
        std::uint32_t m_sentinel_worker_id;
        std::uint64_t m_sentinel_local_ms;

        char m_hostname[64];
        std::uint8_t m_socket_count;
        std::uint32_t m_logical_cores;
        socket_stats m_sockets[MAX_SOCKETS];
        std::uint64_t m_mem_total_bytes;
        std::uint64_t m_mem_avail_bytes;
        net_stats m_net_aggregate;
        disk_stats m_disk_aggregate;
    };

    /// Tag for client-pushed control messages over the dashboard channel.
    enum class control_kind : std::uint16_t {
        UNSPECIFIED = 0,
        /// Promote this worker to its host's sentinel (emits node frames).
        BE_NODE_SENTINEL = 1,
        /// Reset the worker-frame emission interval to @c m_value milliseconds.
        SET_WORKER_INTERVAL_MS = 2,
        /// Reset the node-frame emission interval to @c m_value milliseconds.
        SET_NODE_INTERVAL_MS = 3,
    };

    /// Control message body. The semantics of @ref m_value depend on @ref m_kind.
    struct control_msg {
        std::uint16_t m_version;
        control_kind m_kind;
        std::uint32_t m_value;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_FRAMES_HPP
