#ifndef GEMPBA_TELEMETRY_FRAMES_HPP
#define GEMPBA_TELEMETRY_FRAMES_HPP

#include <cstdint>
#include <vector>

namespace gempba::telemetry {

    /** @brief Hard upper bound on worker_id values in the local transport and the center's assignment table; edges are not capped. */
    inline constexpr unsigned MAX_WORKERS = 1024;
    /** @brief Hard upper bound on per-host socket count; sizes node_frame::m_sockets. */
    inline constexpr unsigned MAX_SOCKETS = 8;

    /**
     * @brief Hard upper bound on a node's logical CPU count, and the derived 64-bit word
     * count for worker_identity's allowed-CPU bitmap. Covers the widest current
     * servers (e.g. dual 192-core SMT) with headroom.
     */
    inline constexpr unsigned MAX_LOGICAL_CPUS = 1024;
    inline constexpr unsigned CPU_MASK_WORDS = MAX_LOGICAL_CPUS / 64;

    /** @brief Wire-format version stamped into every frame. Bump on any layout change. */
    inline constexpr std::uint16_t TELEMETRY_SCHEMA_VERSION = 2;

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
        /** @brief Socket the worker's affinity places it on (0 when affinity is unknown). */
        std::uint8_t m_primary_socket;
        /**
         * @brief Bitmap of the logical CPUs this worker may run on, one bit per CPU
         * across @ref MAX_LOGICAL_CPUS: word @c w bit @c b means CPU @c w*64+b.
         * Wide enough to represent every CPU on big nodes, not just the low 64.
         */
        std::uint64_t m_allowed_cpu_mask[CPU_MASK_WORDS];
    };

    /** @brief Traffic to one destination in @ref worker_frame::m_edges_out. */
    struct edge_counter {
        std::uint32_t m_dst_worker_id;
        std::uint64_t m_bytes;
        std::uint64_t m_count;
    };

    /**
     * @brief The fixed-size part of a @ref worker_frame; it travels over MPI as one block ahead of the edges.
     *
     * Counters are cumulative since worker startup; consumers compute deltas if they want rate-of-change.
     */
    struct worker_frame_header {
        std::uint16_t m_version;
        std::uint32_t m_worker_id;
        std::uint32_t m_seq_no;
        /** @brief Id of the latest @ref control_kind::SNAPSHOT request this rank answered; 0 before any. */
        std::uint32_t m_snapshot_id;
        std::uint64_t m_worker_local_ms;

        /**
         * @brief Subtrees handed to a pool thread (load_balancer::force_local_submit), received subtrees included, counted at
         * hand-off rather than completion. Branches explored inline (load_balancer::forward) are not counted. Not exclusive
         * with @ref m_tasks_sent_total or @ref m_tasks_recv_total.
         */
        std::uint64_t m_tasks_started_total;
        /** @brief Tasks sent to a remote worker. */
        std::uint64_t m_tasks_sent_total;
        /** @brief Tasks received from a remote worker. */
        std::uint64_t m_tasks_recv_total;
        /** @brief Bytes of the tasks sent; the sum of @ref worker_frame::m_edges_out bytes. */
        std::uint64_t m_bytes_sent_total;
        /** @brief Bytes of the tasks received. */
        std::uint64_t m_bytes_recv_total;
        /**
         * @brief Microseconds in the MPI calls that send tasks, runnable id included. This is how long the sender stalled, not
         * wire latency; network delay shows up in assignment latency and waiting time.
         */
        std::uint64_t m_send_microseconds_total;
        /** @brief Microseconds in the MPI calls that receive tasks once a probe found them, runnable id included. */
        std::uint64_t m_recv_microseconds_total;
        /** @brief Local submissions the load balancer refused because every pool thread was busy; those nodes ran inline. */
        std::uint64_t m_spawn_rejected_pool_full;
        /**
         * @brief Remote submissions refused because the scheduler opened no transmission channel (one already in use, the center
         * full, or no target rank); those nodes were then tried locally.
         */
        std::uint64_t m_spawn_remote_rejected;
        /** @brief Tasks currently executing on the worker's thread pool. */
        std::uint32_t m_tasks_running;
        /** @brief Outstanding requests in the scheduler queue. */
        std::uint32_t m_scheduler_pending_count;
        /**
         * @brief Average microseconds a pool thread has been idle since startup
         * (cumulative pool idle time / pool size).
         */
        std::uint64_t m_idle_microseconds_per_worker;
        /** @brief Part of @ref m_idle_microseconds_per_worker spent while no task ran in the pool. */
        std::uint64_t m_idle_microseconds_starved;
        /** @brief Part of @ref m_idle_microseconds_per_worker spent while other pool threads ran tasks. */
        std::uint64_t m_idle_microseconds_imbalanced;

        /** @brief Worker: microseconds from receiving a task to reporting available, summed over its tasks. */
        std::uint64_t m_sched_running_microseconds;
        /** @brief Worker: microseconds waiting for a task since run start; with @ref m_sched_running_microseconds it adds up to the run time. */
        std::uint64_t m_sched_waiting_microseconds;
        /** @brief Worker: milliseconds from run start to its first task; 0 until @ref m_tasks_recv_total is non-zero. */
        std::uint64_t m_first_task_ms;
        /** @brief Worker: milliseconds from run start to the end of its latest task. */
        std::uint64_t m_last_task_ms;
        /**
         * @brief Center: microseconds from assigning a rank to that rank reporting running, summed over assignments.
         * The centralized center assigns by sending a task; the semi-centralized one by naming the rank a push target.
         */
        std::uint64_t m_assign_latency_microseconds_total;
        /** @brief Center: assignments counted in @ref m_assign_latency_microseconds_total. */
        std::uint64_t m_assign_count;
        /** @brief Center: longest single assignment latency, in microseconds. */
        std::uint64_t m_assign_latency_microseconds_max;
        /** @brief Centralized center: microseconds ranks spent available while its queue was empty, summed over ranks. */
        std::uint64_t m_available_microseconds_queue_empty;

        /** @brief Center: running and available reports handled. Tasks in and out are @ref m_tasks_recv_total and @ref m_tasks_sent_total. */
        std::uint64_t m_center_state_msgs_total;
        /** @brief Center: score proposals received, whether or not they improved the global score. */
        std::uint64_t m_center_score_proposals_total;
        /** @brief Center: global score broadcasts, each a message to every worker. */
        std::uint64_t m_center_score_broadcasts_total;
        /** @brief Worker: score proposals sent to the center. */
        std::uint64_t m_score_proposals_sent;
        /** @brief Center: milliseconds from run start to the latest global score improvement; 0 until @ref m_center_score_broadcasts_total is non-zero. */
        std::uint64_t m_last_score_improvement_ms;
        /** @brief Centralized center: microseconds its workers were told the center is full and must not send tasks. */
        std::uint64_t m_center_full_microseconds;
        /**
         * @brief Microseconds this rank spent in telemetry_hub::tick_if_due calls that sent or read a frame; under MPI, time taken
         * from its scheduler thread. Calls with nothing due are part of the scheduler's polling loop and are not counted.
         */
        std::uint64_t m_telemetry_microseconds_total;

        float m_process_cpu_pct;
        std::uint64_t m_process_rss_bytes;
        std::uint32_t m_process_threads;
    };

    /**
     * @brief Periodic per-worker telemetry sample, emitted on the worker-interval cadence.
     *
     * Its size follows the destinations the worker actually sent tasks to, not the size of the run.
     */
    struct worker_frame : worker_frame_header {
        /** @brief One entry per destination this worker sent tasks to, in ascending @c m_dst_worker_id; others are absent. */
        std::vector<edge_counter> m_edges_out;
    };

    /** @brief Per-socket CPU + memory stats inside @ref node_frame::m_sockets. */
    struct socket_stats {
        std::uint8_t m_socket_id;
        float m_cpu_pct;
        std::uint64_t m_mem_total_bytes;
        std::uint64_t m_mem_used_bytes;
    };

    /**
     * @brief Periodic per-host telemetry sample.
     *
     * Emitted by the host's sentinel worker with each of its worker frames.
     * Aggregates host-wide signals (CPU, memory) that are not meaningful
     * per-worker.
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
        /**
         * @brief Job-cgroup memory, or 0 when this process is not in a memory cgroup
         * (or the limit is effectively unlimited): @c m_cgroup_mem_used_bytes is
         * the job-step's current memory on this host and @c m_cgroup_mem_limit_bytes
         * its allocation. Lets a consumer show "gempba's memory / its allocation"
         * instead of the host-wide total on a shared node. Scheduler-agnostic
         * (any cgroup-based scheduler populates these).
         */
        std::uint64_t m_cgroup_mem_used_bytes;
        std::uint64_t m_cgroup_mem_limit_bytes;
    };

    /** @brief Tag for client-pushed control messages over the dashboard channel. */
    enum class control_kind : std::uint16_t {
        UNSPECIFIED = 0,
        /** @brief Promote this worker to its host's sentinel (emits node frames). */
        BE_NODE_SENTINEL = 1,
        /** @brief Reset the telemetry interval, for worker and node frames alike, to @c m_value milliseconds. */
        SET_WORKER_INTERVAL_MS = 2,
        /** @brief Stop publishing and ingesting worker and node frames until @ref RESUME; per-event counters keep counting, and shutdown still sends the final frames. */
        PAUSE = 3,
        /** @brief Publish and ingest frames again at the current interval. */
        RESUME = 4,
        /**
         * @brief Publish one worker frame, and a sentinel's node frame, on the next tick regardless of the interval or a pause;
         * @c m_value is the request id the frame echoes in @ref worker_frame_header::m_snapshot_id.
         */
        SNAPSHOT = 5,
    };

    /** @brief Control message body. The semantics of @ref m_value depend on @ref m_kind. */
    struct control_msg {
        std::uint16_t m_version;
        control_kind m_kind;
        std::uint32_t m_value;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_FRAMES_HPP
