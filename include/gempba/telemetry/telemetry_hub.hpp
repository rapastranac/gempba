#ifndef GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP
#define GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP

#include <atomic>
#include <cstdint>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/telemetry_transport.hpp>
#include <gempba/telemetry/topology.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace gempba::telemetry {

    class process_probe;
    class node_probe;
    class center_tcp_server;

    class telemetry_hub {
    public:
        telemetry_hub();
        ~telemetry_hub();

        telemetry_hub(const telemetry_hub&) = delete;
        telemetry_hub& operator=(const telemetry_hub&) = delete;

        // p_start_pump_thread=false skips the MT-only timer thread (and the
        // center TCP server when that is wired); tick_if_due must then be
        // driven manually.
        void on_runtime_ready(runtime_mode p_mode, std::uint32_t p_worker_id, std::uint32_t p_world_size, bool p_start_pump_thread = true);

        void shutdown();

        // Hot-path event hooks. Atomic increments, no locks, no allocation.
        void record_send(std::uint32_t p_dst_worker_id, std::uint64_t p_bytes) noexcept;
        void record_recv(std::uint32_t p_src_worker_id, std::uint64_t p_bytes) noexcept;
        void record_task_local() noexcept;

        // Pump entry point. Cheap when not due (one timestamp compare).
        void tick_if_due() noexcept;

        // Snapshot is written by the pump thread; safe alongside concurrent
        // record_* calls (relaxed atomics). Not const — the process probe
        // maintains delta state for CPU%.
        void snapshot_worker_frame(worker_frame& p_out) noexcept;
        void snapshot_node_frame(node_frame& p_out) noexcept;

        [[nodiscard]] std::optional<worker_frame> latest_worker_frame(std::uint32_t p_worker_id) const;
        [[nodiscard]] std::optional<node_frame> latest_node_frame(std::uint32_t p_sentinel_worker_id) const;
        [[nodiscard]] const topology_snapshot& current_topology() const noexcept;
        [[nodiscard]] std::uint64_t program_elapsed_seconds() const noexcept;

        void set_worker_interval_ms(std::uint32_t p_interval_ms) noexcept;
        void set_node_interval_ms(std::uint32_t p_interval_ms) noexcept;
        void mark_as_node_sentinel() noexcept;

        [[nodiscard]] std::uint32_t worker_interval_ms() const noexcept;
        [[nodiscard]] std::uint32_t node_interval_ms() const noexcept;

        void apply_control_from_client(control_kind p_kind, std::uint32_t p_value) noexcept;

    private:
        struct counters;
        struct aggregator;

        runtime_mode m_mode = runtime_mode::MT_ONLY;
        std::uint32_t m_worker_id = 0;
        std::uint32_t m_world_size = 1;
        std::atomic<bool> m_is_sentinel{false};
        std::atomic<std::uint32_t> m_worker_interval_ms{500};
        std::atomic<std::uint32_t> m_node_interval_ms{1000};
        std::atomic<std::uint64_t> m_last_worker_emit_ms{0};
        std::atomic<std::uint64_t> m_last_node_emit_ms{0};
        std::atomic<std::uint32_t> m_seq_no{0};
        std::atomic<std::uint64_t> m_start_ms{0};

        std::unique_ptr<counters> m_counters;
        std::unique_ptr<process_probe> m_process_probe;
        std::unique_ptr<node_probe> m_node_probe;
        std::unique_ptr<telemetry_transport> m_transport;
        std::unique_ptr<aggregator> m_aggregator; // center role only
        std::unique_ptr<center_tcp_server> m_tcp_server; // center role only
        topology_snapshot m_topology;
        std::optional<std::thread> m_timer_thread;
        std::atomic<bool> m_running{false};

        mutable std::mutex m_pending_fanout_mtx;
        std::vector<control_msg> m_pending_fanout;
    };

    // Null when telemetry is OFF or before install().
    telemetry_hub* get() noexcept;

    void install(std::unique_ptr<telemetry_hub> p_hub) noexcept;
    void uninstall() noexcept;

    // Sets the TCP port the center will bind. Must be called before the hub is
    // installed (i.e., before the first node_manager / scheduler is created).
    // Default is 9000 if never called.
    void configure_port(std::uint16_t p_port) noexcept;

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP
