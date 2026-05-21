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

    /**
     * @brief Process-wide runtime-telemetry hub.
     *
     * Owns the transport, counters, probes, and (on the center role) the
     * dashboard server. Workers feed it via the @c record_* hot-path hooks;
     * the pump thread periodically snapshots the counters into frames and
     * pushes them through the transport.
     *
     * One hub per process. Use the free functions @ref install, @ref get,
     * @ref uninstall to manage the singleton.
     */
    class telemetry_hub {
    public:
        telemetry_hub();
        ~telemetry_hub();

        telemetry_hub(const telemetry_hub&) = delete;
        telemetry_hub& operator=(const telemetry_hub&) = delete;

        /**
         * @brief Bind the hub to a runtime mode and start the transport.
         *
         * Call once after the runtime is up and the worker_id / world_size
         * are known. Sets up the transport, populates the topology snapshot,
         * and (on the center role) starts the dashboard server.
         *
         * @param p_mode              Runtime mode driving transport choice.
         * @param p_worker_id         This worker's id (0..world_size-1).
         * @param p_world_size        Total worker count in this run.
         * @param p_start_pump_thread When @c false, the periodic pump thread
         *                            (and the center dashboard server, when
         *                            wired) is skipped; @ref tick_if_due must
         *                            then be called manually. Used by tests.
         */
        void on_runtime_ready(runtime_mode p_mode, std::uint32_t p_worker_id, std::uint32_t p_world_size, bool p_start_pump_thread = true);

        /// Stop the pump, drain a final frame, and release transport resources.
        void shutdown();

        /**
         * @brief Record an outbound task to a remote worker (bytes + count).
         *
         * Atomic, lock-free, allocation-free. Safe to call from any thread.
         */
        void record_send(std::uint32_t p_dst_worker_id, std::uint64_t p_bytes) noexcept;

        /// Record an inbound task from a remote worker. Hot-path; same guarantees as @ref record_send.
        void record_recv(std::uint32_t p_src_worker_id, std::uint64_t p_bytes) noexcept;

        /// Record a task dispatched to the local thread pool. Hot-path; same guarantees as @ref record_send.
        void record_task_local() noexcept;

        /**
         * @brief Emit pending frames if their cadence is due, else return cheaply.
         *
         * One timestamp compare in the not-due path. Driven by the pump thread
         * by default; callers that pass @c p_start_pump_thread=false to
         * @ref on_runtime_ready must drive this manually.
         */
        void tick_if_due() noexcept;

        /**
         * @brief Fill @p p_out with the current worker counter snapshot.
         *
         * Safe alongside concurrent @c record_* calls (relaxed atomics).
         * Not @c const — the process probe maintains delta state for CPU%.
         */
        void snapshot_worker_frame(worker_frame& p_out) noexcept;

        /// Fill @p p_out with the current host-level snapshot. Sentinel workers only.
        void snapshot_node_frame(node_frame& p_out) noexcept;

        /// @return The latest worker_frame for @p p_worker_id, if one has been received.
        [[nodiscard]] std::optional<worker_frame> latest_worker_frame(std::uint32_t p_worker_id) const;

        /// @return The latest node_frame for @p p_sentinel_worker_id, if one has been received.
        [[nodiscard]] std::optional<node_frame> latest_node_frame(std::uint32_t p_sentinel_worker_id) const;

        /// Static topology captured at @ref on_runtime_ready time.
        [[nodiscard]] const topology_snapshot& current_topology() const noexcept;

        /// Wall-clock seconds since @ref on_runtime_ready was called; 0 before that.
        [[nodiscard]] std::uint64_t program_elapsed_seconds() const noexcept;

        /// Set the cadence at which worker frames are emitted. Fans out to other workers.
        void set_worker_interval_ms(std::uint32_t p_interval_ms) noexcept;

        /// Set the cadence at which node frames are emitted (sentinel only). Fans out.
        void set_node_interval_ms(std::uint32_t p_interval_ms) noexcept;

        /// Promote this worker to its host's sentinel; it will start emitting node frames.
        void mark_as_node_sentinel() noexcept;

        /// @return The current worker-frame emission interval, in milliseconds.
        [[nodiscard]] std::uint32_t worker_interval_ms() const noexcept;

        /// @return The current node-frame emission interval, in milliseconds.
        [[nodiscard]] std::uint32_t node_interval_ms() const noexcept;

        /**
         * @brief Apply one control message received from the dashboard.
         *
         * Routed by the center role; workers act on it locally. See
         * @ref control_kind for the semantics per kind.
         */
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

    /// @return The installed hub, or @c nullptr when none is installed.
    telemetry_hub* get() noexcept;

    /// Install @p p_hub as the process-wide singleton; transfers ownership.
    void install(std::unique_ptr<telemetry_hub> p_hub) noexcept;

    /// Tear down and destroy the installed hub. No-op when none is installed.
    void uninstall() noexcept;

    /**
     * @brief Tear down the installed hub and block subsequent @ref install calls until @ref enable.
     *
     * Process-local and sticky. Must be called symmetrically across every
     * participating rank (or none): disabling on a subset will deadlock the
     * still-enabled ranks during transport setup. The recommended pattern is
     * to call @ref disable on every rank before any @c create_* runtime entry
     * point.
     */
    void disable() noexcept;

    /// Lift a prior @ref disable; subsequent @ref install calls take effect again.
    void enable() noexcept;

    /// @return @c true when the kill switch is in the @ref enable state.
    [[nodiscard]] bool is_enabled() noexcept;

    /**
     * @brief Set the TCP port the center role will bind for the dashboard.
     *
     * Must be called before the hub is installed (i.e., before the first
     * node_manager / scheduler is created). Default is 9000 if never called.
     */
    void configure_port(std::uint16_t p_port) noexcept;

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP
