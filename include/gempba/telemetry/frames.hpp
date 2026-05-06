#ifndef GEMPBA_TELEMETRY_FRAMES_HPP
#define GEMPBA_TELEMETRY_FRAMES_HPP

#include <cstdint>

namespace gempba::telemetry {

    inline constexpr unsigned MAX_WORKERS = 1024;
    inline constexpr unsigned MAX_SOCKETS = 8;

    inline constexpr std::uint16_t TELEMETRY_SCHEMA_VERSION = 1;

    struct worker_identity {
        std::uint16_t m_version;
        std::uint32_t m_worker_id;
        char m_hostname[64];
        std::uint32_t m_pid;
        std::uint8_t m_primary_socket;
        std::uint64_t m_allowed_cpu_mask;
    };

    struct edge_counter {
        std::uint64_t m_bytes;
        std::uint64_t m_count;
    };

    struct worker_frame {
        std::uint16_t m_version;
        std::uint32_t m_worker_id;
        std::uint32_t m_seq_no;
        std::uint64_t m_worker_local_ms;

        // tasks_local_total and tasks_sent_total are mutually exclusive: a task
        // event is either dispatched to the local pool or sent to a remote rank.
        std::uint64_t m_tasks_local_total;
        std::uint64_t m_tasks_sent_total;
        std::uint64_t m_tasks_recv_total;
        std::uint32_t m_tasks_running;
        std::uint32_t m_scheduler_pending_count;
        // Average microseconds a pool thread has spent idle since startup
        // (cumulative pool idle time divided by the pool size).
        std::uint64_t m_idle_microseconds_per_worker;

        float m_process_cpu_pct;
        std::uint64_t m_process_rss_bytes;
        std::uint32_t m_process_threads;

        edge_counter m_edges_out[MAX_WORKERS];
    };

    struct socket_stats {
        std::uint8_t m_socket_id;
        float m_cpu_pct;
        std::uint64_t m_mem_total_bytes;
        std::uint64_t m_mem_used_bytes;
    };

    struct net_stats {
        std::uint64_t m_bytes_in;
        std::uint64_t m_bytes_out;
        std::uint64_t m_packets_in;
        std::uint64_t m_packets_out;
    };

    struct disk_stats {
        std::uint64_t m_read_bytes;
        std::uint64_t m_write_bytes;
    };

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

    enum class control_kind : std::uint16_t {
        UNSPECIFIED = 0,
        BE_NODE_SENTINEL = 1,
        SET_WORKER_INTERVAL_MS = 2,
        SET_NODE_INTERVAL_MS = 3,
    };

    struct control_msg {
        std::uint16_t m_version;
        control_kind m_kind;
        std::uint32_t m_value;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_FRAMES_HPP
