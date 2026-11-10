#ifndef GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP
#define GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/topology.hpp>
#include <gempba/utils/gempba_utils.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gempba::telemetry {

    /** @brief Most annotations one run keeps; see @ref telemetry_hub::annotate_run. */
    inline constexpr std::size_t MAX_RUN_ANNOTATIONS = 8;
    /** @brief Longest annotation key, in bytes. */
    inline constexpr std::size_t MAX_RUN_ANNOTATION_KEY_BYTES = 32;
    /** @brief Longest annotation value, in bytes. */
    inline constexpr std::size_t MAX_RUN_ANNOTATION_VALUE_BYTES = 64;
    /** @brief Most markers one run keeps; see @ref telemetry_hub::mark_run. */
    inline constexpr std::size_t MAX_RUN_MARKERS = 32;
    /** @brief Longest marker label, in bytes. */
    inline constexpr std::size_t MAX_RUN_MARKER_LABEL_BYTES = 64;

    class process_probe;
    class node_probe;
    class aggregator_tcp_server;
    class telemetry_transport;

    /**
     * @brief Process-wide runtime-telemetry hub.
     *
     * Owns the transport, counters, probes, and (on the aggregator) the
     * dashboard server. Each rank feeds it via the @c record_* hot-path hooks;
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
         * Call once after the runtime is up and the rank / world_size
         * are known. Sets up the transport, populates the topology snapshot,
         * and (on the aggregator) starts the dashboard server.
         *
         * @param p_mode              Runtime mode driving transport choice.
         * @param p_rank              This rank (0..world_size-1).
         * @param p_world_size        Total rank count in this run.
         * @param p_start_pump_thread When @c false, the periodic pump thread
         *                            (and the aggregator's dashboard server, when
         *                            wired) is skipped; @ref tick_if_due must
         *                            then be called manually. Used by tests.
         */
        void on_runtime_ready(runtime_mode p_mode, std::uint32_t p_rank, std::uint32_t p_world_size, bool p_start_pump_thread = true);

        /// Stop the pump, drain a final frame, and release transport resources.
        void shutdown();

        /**
         * @brief Record an outbound task to another rank (bytes + count), and how long its MPI sends took.
         *
         * Atomic, lock-free, allocation-free. Safe to call from any thread.
         */
        void record_send(std::uint32_t p_dst_rank, std::uint64_t p_bytes, std::chrono::nanoseconds p_elapsed = {}) noexcept;

        /// Record an inbound task from another rank, and how long its MPI receives took. Hot-path; same guarantees as @ref record_send.
        void record_recv(std::uint32_t p_src_rank, std::uint64_t p_bytes, std::chrono::nanoseconds p_elapsed = {}) noexcept;

        /// Record a task dispatched to the local thread pool. Hot-path; same guarantees as @ref record_send.
        void record_task_local() noexcept;

        /// Record the start of this rank's scheduler run; scheduler times accrue from here and task times are relative to it.
        void record_run_start() noexcept;

        /// Record the end of this rank's scheduler run; scheduler times stop accruing.
        void record_run_end() noexcept;

        /// Record that this worker waits for a task: at run start and after reporting available.
        void record_worker_waiting() noexcept;

        /// Record that this worker received a task and runs it.
        void record_worker_running() noexcept;

        /// Record that the coordinator assigned rank @p p_rank, which then waits for its task.
        void record_assignment(std::uint32_t p_rank) noexcept;

        /// Record that rank @p p_rank reported running, which ends its pending assignment.
        void record_assignment_accepted(std::uint32_t p_rank) noexcept;

        /// Record the coordinator's available-rank count and queue state after it handled an event.
        void record_coordinator_queue(std::uint32_t p_available_ranks, bool p_queue_empty) noexcept;

        /// Record that the coordinator handled a running or available report. Hot-path; same guarantees as @ref record_send.
        void record_coordinator_state_msg() noexcept;

        /// Record that the coordinator received a score proposal. Hot-path; same guarantees as @ref record_send.
        void record_coordinator_score_proposal() noexcept;

        /// Record that the coordinator broadcast a new global score to every worker. Hot-path; same guarantees as @ref record_send.
        void record_coordinator_score_broadcast() noexcept;

        /// Record that the coordinator told its workers it is full; repeated notices extend the same full period.
        void record_coordinator_full() noexcept;

        /// Record that the coordinator told its workers it has room again.
        void record_coordinator_free() noexcept;

        /// Record that this worker proposed its local best score to the coordinator. Hot-path; same guarantees as @ref record_send.
        void record_score_proposal_sent() noexcept;

        /// Record that the coordinator's global score improved; its time since run start becomes the latest improvement.
        void record_global_score_improved() noexcept;

        /**
         * @brief Emit pending frames if their cadence is due, else return cheaply.
         *
         * One timestamp compare in the not-due path. Driven by the pump thread
         * by default; callers that pass @c p_start_pump_thread=false to
         * @ref on_runtime_ready must drive this manually.
         */
        void tick_if_due() noexcept;

        /**
         * @brief Fill @p p_out with this rank's current counter snapshot.
         *
         * Safe alongside concurrent @c record_* calls (relaxed atomics).
         * Not @c const — the process probe maintains delta state for CPU%.
         */
        void snapshot_rank_frame(rank_frame& p_out) noexcept;

        /// Fill @p p_out with the current host-level snapshot. Sentinel ranks only.
        void snapshot_node_frame(node_frame& p_out) noexcept;

        /// @return The latest frame of rank @p p_rank, if one has been received.
        [[nodiscard]] std::optional<rank_frame> latest_rank_frame(std::uint32_t p_rank) const;

        /// @return The latest node_frame of the host whose sentinel is rank @p p_sentinel_rank, if one has been received.
        [[nodiscard]] std::optional<node_frame> latest_node_frame(std::uint32_t p_sentinel_rank) const;

        /// Static topology captured at @ref on_runtime_ready time.
        [[nodiscard]] const topology_snapshot& current_topology() const noexcept;

        /// Wall-clock seconds since @ref on_runtime_ready was called; 0 before that.
        [[nodiscard]] std::uint64_t program_elapsed_seconds() const noexcept;

        /// The UUID v4 the aggregator generated for this run at @ref on_runtime_ready; empty on other ranks.
        [[nodiscard]] const std::string& run_id() const noexcept;

        /**
         * @brief Attach @p p_value to this run under @p p_key, e.g. a run label, problem name or seed.
         *
         * The aggregator broadcasts its annotations with the run setup. Keeps up to @ref MAX_RUN_ANNOTATIONS pairs, keys of 1 to
         * @ref MAX_RUN_ANNOTATION_KEY_BYTES bytes and values up to @ref MAX_RUN_ANNOTATION_VALUE_BYTES bytes; a pair over a limit
         * is dropped with a warning, never truncated. Annotating a key again replaces its value.
         *
         * @return whether the pair was kept
         */
        bool annotate_run(std::string_view p_key, std::string_view p_value);

        /// @return this run's annotations, in the order their keys were first set.
        [[nodiscard]] std::vector<std::pair<std::string, std::string>> run_annotations() const;

        /**
         * @brief Record that @p p_label happened now, e.g. "phase 2 started", stamped in milliseconds since telemetry came up.
         *
         * Only the aggregator keeps markers, and it broadcasts them with the run setup; on any other rank, before
         * @ref on_runtime_ready or after @ref shutdown, the call does nothing.
         * Keeps up to @ref MAX_RUN_MARKERS markers with labels of 1 to @ref MAX_RUN_MARKER_LABEL_BYTES bytes; a marker over a
         * limit is dropped with a warning, never truncated.
         *
         * @return whether the marker was kept
         */
        bool mark_run(std::string_view p_label, marker_source p_source = marker_source::CODE);

        /// @return this run's markers, in the order they were recorded.
        [[nodiscard]] std::vector<run_marker> run_markers() const;

        /// Record the scheduler this run uses, by its scheduler::get_name(), and this rank's own role, its scheduler::role_of(rank_me()); call it before @ref on_runtime_ready.
        void describe_scheduler(std::string_view p_name, rank_role p_role = rank_role::WORKER);

        /// Record the load balancer this run uses.
        void describe_load_balancer(balancing_policy p_policy);

        /// @return how this run is set up; the thread count comes from the latest rank frames, so only the aggregator fills it.
        [[nodiscard]] run_description describe_run() const;

        /// Set the telemetry interval at which rank frames, and each host sentinel's node frame, are emitted. Fans out to other ranks.
        void set_interval_ms(std::uint32_t p_interval_ms) noexcept;

        /// Promote this rank to its host's sentinel; it will start emitting node frames.
        void mark_as_node_sentinel() noexcept;

        /// @return The current telemetry interval, in milliseconds.
        [[nodiscard]] std::uint32_t interval_ms() const noexcept;

        /// @return whether a @ref control_kind::PAUSE holds; frames are neither published nor ingested until a @ref control_kind::RESUME.
        [[nodiscard]] bool paused() const noexcept;

        /**
         * @return the id of the latest accepted @ref control_kind::SNAPSHOT request, counting from 1; 0 before any. A request
         * within 50 ms of the last accepted one collapses into it.
         */
        [[nodiscard]] std::uint32_t snapshot_requested() const noexcept;

        /**
         * @return whether the aggregator still waits for a frame from some rank answering the latest snapshot request. It ingests
         * frames while it waits, even when paused, and gives up one second after the request.
         */
        [[nodiscard]] bool snapshot_outstanding() const;

        /**
         * @brief Apply one control message received from the dashboard.
         *
         * Routed by the aggregator; other ranks act on it locally. See
         * @ref control_kind for the semantics per kind.
         */
        void apply_control_from_client(control_kind p_kind, std::uint32_t p_value) noexcept;

    private:
        struct counters;
        struct aggregator;

        bool drain_inbound_frames();

        runtime_mode m_mode = runtime_mode::MT_ONLY;
        std::uint32_t m_rank = 0;
        std::uint32_t m_world_size = 1;
        std::atomic<bool> m_is_sentinel{false};
        std::atomic<std::uint32_t> m_interval_ms{500};
        std::atomic<bool> m_paused{false};
        std::atomic<std::uint32_t> m_snapshot_requested{0};
        std::atomic<std::uint64_t> m_snapshot_requested_ms{0};
        std::atomic<std::uint32_t> m_snapshot_due{0};
        std::atomic<std::uint32_t> m_snapshot_answered{0};
        std::atomic<std::uint64_t> m_last_rank_emit_ms{0};
        std::atomic<std::uint32_t> m_seq_no{0};
        std::atomic<std::uint64_t> m_start_ms{0};
        std::string m_run_id;

        std::unique_ptr<counters> m_counters;
        std::unique_ptr<process_probe> m_process_probe;
        std::unique_ptr<node_probe> m_node_probe;
        std::unique_ptr<telemetry_transport> m_transport;
        std::unique_ptr<aggregator> m_aggregator; // aggregator only
        std::unique_ptr<aggregator_tcp_server> m_tcp_server; // aggregator only
        topology_snapshot m_topology;
        std::optional<std::thread> m_timer_thread;
        std::atomic<bool> m_running{false};

        mutable std::mutex m_pending_fanout_mtx;
        std::vector<control_msg> m_pending_fanout;

        mutable std::mutex m_run_mtx;
        std::vector<std::pair<std::string, std::string>> m_run_annotations;
        std::vector<run_marker> m_run_markers;
        std::string m_scheduler_name;
        rank_role m_scheduler_role = rank_role::WORKER;
        std::vector<rank_role> m_rank_roles;
        std::string m_load_balancer_name;
    };

    /// @return The installed hub, or @c nullptr when none is installed.
    telemetry_hub* get() noexcept;

    /**
     * @brief Annotate the current run on the installed hub; see @ref telemetry_hub::annotate_run.
     *
     * With telemetry enabled but no hub installed yet, the pair is dropped with a warning.
     *
     * @return whether the pair was kept
     */
    bool annotate_run(std::string_view p_key, std::string_view p_value);

    /**
     * @brief Record a marker on the installed hub; see @ref telemetry_hub::mark_run.
     *
     * With telemetry enabled but no hub installed yet, the marker is dropped with a warning.
     *
     * @return whether the marker was kept
     */
    bool mark_run(std::string_view p_label);

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
     * @brief Set the TCP port the aggregator will bind for the dashboard.
     *
     * Must be called before the hub is installed (i.e., before the first
     * node_manager / scheduler is created). Default is 9000 if never called.
     */
    void configure_port(std::uint16_t p_port) noexcept;

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_HUB_HPP
