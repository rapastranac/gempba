#include <gempba/config.h>
#include <gempba/core/load_balancer.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <impl/telemetry/center_tcp_server.hpp>
#include <impl/telemetry/local_transport.hpp>
#include <impl/telemetry/node_probe.hpp>
#include <impl/telemetry/process_probe.hpp>
#include <impl/telemetry/run_id.hpp>
#include <impl/telemetry/scheduler_clock.hpp>
#include <impl/telemetry/topology_builder.hpp>

#if GEMPBA_MULTIPROCESSING
    #include <impl/telemetry/mpi_transport.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace gempba::telemetry {

    namespace {
        std::unique_ptr<telemetry_hub> g_owner;
        // Telemetry is ON by default; the kill switch is opt-out.
        bool g_enabled = true;

        std::uint64_t monotonic_ms() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        std::uint64_t monotonic_us() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count());
        }

        constexpr std::chrono::milliseconds k_pump_resolution{50};
        constexpr std::uint64_t k_final_frame_wait_ms = 1000;
        constexpr std::chrono::milliseconds k_final_frame_poll{10};

        constexpr std::uint16_t k_default_telemetry_port = 9000;
        std::atomic<std::uint16_t> g_configured_port{k_default_telemetry_port};
    } // namespace

    void configure_port(const std::uint16_t p_port) noexcept { g_configured_port.store(p_port, std::memory_order_release); }

    struct telemetry_hub::counters {
        std::atomic<std::uint64_t> m_tasks_started_total{0};
        std::atomic<std::uint64_t> m_tasks_sent_total{0};
        std::atomic<std::uint64_t> m_tasks_recv_total{0};
        std::atomic<std::uint64_t> m_bytes_sent_total{0};
        std::atomic<std::uint64_t> m_bytes_recv_total{0};
        std::atomic<std::uint64_t> m_send_nanoseconds_total{0};
        std::atomic<std::uint64_t> m_recv_nanoseconds_total{0};

        std::atomic<std::uint64_t> m_center_state_msgs_total{0};
        std::atomic<std::uint64_t> m_center_score_proposals_total{0};
        std::atomic<std::uint64_t> m_center_score_broadcasts_total{0};
        std::atomic<std::uint64_t> m_score_proposals_sent{0};
        std::atomic<std::uint64_t> m_telemetry_microseconds_total{0};

        std::atomic<std::uint64_t> m_edges_out_bytes[MAX_WORKERS]{};
        std::atomic<std::uint64_t> m_edges_out_count[MAX_WORKERS]{};

        std::mutex m_scheduler_mtx;
        scheduler_clock m_scheduler_clock;
    };

    // Center-side latest-frame state. Lives only on the center role; sparse
    // maps avoid the 16 KB-per-slot cost up front.
    struct telemetry_hub::aggregator {
        mutable std::mutex m_mtx;
        std::map<std::uint32_t, worker_frame> m_latest_worker;
        std::map<std::uint32_t, node_frame> m_latest_node;
        std::map<std::uint32_t, std::uint64_t> m_last_seen_ms;
    };

    telemetry_hub::telemetry_hub() : m_counters(std::make_unique<counters>()), m_process_probe(std::make_unique<process_probe>()), m_node_probe(std::make_unique<node_probe>()) {}

    telemetry_hub::~telemetry_hub() { shutdown(); }

    void telemetry_hub::on_runtime_ready(const runtime_mode p_mode, const std::uint32_t p_worker_id, const std::uint32_t p_world_size, const bool p_start_pump_thread) {
        m_mode = p_mode;
        m_worker_id = p_worker_id;
        m_world_size = p_world_size;
        m_start_ms.store(monotonic_ms(), std::memory_order_release);

        switch (p_mode) {
            case runtime_mode::MT_ONLY:
                m_transport = std::make_unique<local_transport>();
                break;
            case runtime_mode::MP_MPI:
#if GEMPBA_MULTIPROCESSING
                m_transport = std::make_unique<mpi_transport>();
#else
                m_transport = std::make_unique<local_transport>();
#endif
                break;
        }

        if (p_worker_id == 0) {
            m_run_id = detail::generate_run_id();
            m_aggregator = std::make_unique<aggregator>();
            if (p_start_pump_thread) {
                m_tcp_server = std::make_unique<center_tcp_server>(g_configured_port.load(std::memory_order_acquire), *this);
                (void) m_tcp_server->start();
            }
        }

        m_topology = build_topology_snapshot(p_mode, p_worker_id, p_world_size);
        for (const auto& v_node: m_topology.m_nodes) {
            if (v_node.m_sentinel_worker_id == p_worker_id) {
                m_is_sentinel.store(true, std::memory_order_relaxed);
                break;
            }
        }

        m_running.store(true, std::memory_order_release);

        if (p_mode == runtime_mode::MT_ONLY && m_transport && p_start_pump_thread) {
            m_timer_thread.emplace([this] {
                while (m_running.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(k_pump_resolution);
                    tick_if_due();
                }
            });
        }
    }

    void telemetry_hub::shutdown() {
        if (!m_running.exchange(false, std::memory_order_acq_rel))
            return;

        if (m_timer_thread && m_timer_thread->joinable())
            m_timer_thread->join();
        m_timer_thread.reset();

        // Force a final snapshot so the aggregator (and any consumer reading
        // it) reflects post-wait counters, not the snapshot taken up to one
        // worker_interval_ms before completion.
        if (m_transport) {
            m_seq_no.fetch_add(1, std::memory_order_relaxed);
            worker_frame v_final{};
            snapshot_worker_frame(v_final);
            m_transport->publish_final_worker(v_final);
            if (m_is_sentinel.load(std::memory_order_relaxed)) {
                node_frame v_node_final{};
                snapshot_node_frame(v_node_final);
                m_transport->publish_node(v_node_final);
            }
            if (m_aggregator) {
                drain_inbound_frames();
                // Ranks shut down independently; give late ranks a bounded window so their final frames reach the dashboard.
                const std::uint64_t v_deadline = monotonic_ms() + k_final_frame_wait_ms;
                while (m_world_size > 1 && m_transport->final_frames_received() < m_world_size && monotonic_ms() < v_deadline) {
                    std::this_thread::sleep_for(k_final_frame_poll);
                    drain_inbound_frames();
                }
            }
        }

        if (m_tcp_server) {
            m_tcp_server->stop();
            m_tcp_server.reset();
        }
        m_transport.reset();
        m_aggregator.reset();
    }

    void telemetry_hub::record_send(const std::uint32_t p_dst_worker_id, const std::uint64_t p_bytes, const std::chrono::nanoseconds p_elapsed) noexcept {
        if (p_dst_worker_id < MAX_WORKERS) {
            m_counters->m_edges_out_bytes[p_dst_worker_id].fetch_add(p_bytes, std::memory_order_relaxed);
            m_counters->m_edges_out_count[p_dst_worker_id].fetch_add(1, std::memory_order_relaxed);
        }
        m_counters->m_tasks_sent_total.fetch_add(1, std::memory_order_relaxed);
        m_counters->m_bytes_sent_total.fetch_add(p_bytes, std::memory_order_relaxed);
        m_counters->m_send_nanoseconds_total.fetch_add(static_cast<std::uint64_t>(p_elapsed.count()), std::memory_order_relaxed);
    }

    void telemetry_hub::record_recv(std::uint32_t, const std::uint64_t p_bytes, const std::chrono::nanoseconds p_elapsed) noexcept {
        m_counters->m_tasks_recv_total.fetch_add(1, std::memory_order_relaxed);
        m_counters->m_bytes_recv_total.fetch_add(p_bytes, std::memory_order_relaxed);
        m_counters->m_recv_nanoseconds_total.fetch_add(static_cast<std::uint64_t>(p_elapsed.count()), std::memory_order_relaxed);
    }

    void telemetry_hub::record_task_local() noexcept { m_counters->m_tasks_started_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_run_start() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_run_start(monotonic_us());
    }

    void telemetry_hub::record_run_end() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_run_end(monotonic_us());
    }

    void telemetry_hub::record_worker_waiting() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_worker_waiting(monotonic_us());
    }

    void telemetry_hub::record_worker_running() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_worker_running(monotonic_us());
    }

    void telemetry_hub::record_assignment(const std::uint32_t p_worker_id) noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_assignment(p_worker_id, monotonic_us());
    }

    void telemetry_hub::record_assignment_accepted(const std::uint32_t p_worker_id) noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_assignment_accepted(p_worker_id, monotonic_us());
    }

    void telemetry_hub::record_center_queue(const std::uint32_t p_available_ranks, const bool p_queue_empty) noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_center_queue(p_available_ranks, p_queue_empty, monotonic_us());
    }

    void telemetry_hub::record_center_state_msg() noexcept { m_counters->m_center_state_msgs_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_center_score_proposal() noexcept { m_counters->m_center_score_proposals_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_center_score_broadcast() noexcept { m_counters->m_center_score_broadcasts_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_center_full() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_center_full(monotonic_us());
    }

    void telemetry_hub::record_center_free() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_center_free(monotonic_us());
    }

    void telemetry_hub::record_score_proposal_sent() noexcept { m_counters->m_score_proposals_sent.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_global_score_improved() noexcept {
        const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
        m_counters->m_scheduler_clock.on_global_score_improved(monotonic_us());
    }

    void telemetry_hub::tick_if_due() noexcept {
        if (!m_transport)
            return;

        const std::uint64_t v_start_us = monotonic_us();
        const std::uint64_t v_now = v_start_us / 1000;
        const std::uint32_t v_worker_interval = m_worker_interval_ms.load(std::memory_order_relaxed);
        bool v_moved_frames = false;

        if (v_now - m_last_worker_emit_ms.load(std::memory_order_relaxed) >= v_worker_interval) {
            m_seq_no.fetch_add(1, std::memory_order_relaxed);
            worker_frame v_frame{};
            snapshot_worker_frame(v_frame);
            m_transport->publish_worker(v_frame);
            if (m_is_sentinel.load(std::memory_order_relaxed)) {
                node_frame v_node_frame{};
                snapshot_node_frame(v_node_frame);
                m_transport->publish_node(v_node_frame);
            }
            m_last_worker_emit_ms.store(v_now, std::memory_order_relaxed);
            v_moved_frames = true;
        }

        while (auto v_ctrl = m_transport->try_recv_control()) {
            switch (v_ctrl->m_kind) {
                case control_kind::UNSPECIFIED:
                    break;
                case control_kind::SET_WORKER_INTERVAL_MS:
                    m_worker_interval_ms.store(v_ctrl->m_value, std::memory_order_relaxed);
                    break;
                case control_kind::BE_NODE_SENTINEL:
                    m_is_sentinel.store(true, std::memory_order_relaxed);
                    break;
            }
        }

        std::vector<control_msg> v_pending;
        {
            const std::scoped_lock v_lock(m_pending_fanout_mtx);
            v_pending.swap(m_pending_fanout);
        }
        for (const auto& v_msg: v_pending) {
            for (std::uint32_t v_id = 0; v_id < m_world_size; ++v_id) {
                if (v_id == m_worker_id)
                    continue;
                m_transport->send_control(v_id, v_msg);
            }
        }

        if (m_aggregator && drain_inbound_frames()) {
            v_moved_frames = true;
        }

        if (v_moved_frames) {
            m_counters->m_telemetry_microseconds_total.fetch_add(monotonic_us() - v_start_us, std::memory_order_relaxed);
        }
    }

    bool telemetry_hub::drain_inbound_frames() {
        const std::uint64_t v_ts = monotonic_ms();
        bool v_received = false;
        m_transport->poll(
                [this, v_ts, &v_received](const std::uint32_t p_src, const worker_frame& p_frame) {
                    const std::scoped_lock v_lock(m_aggregator->m_mtx);
                    m_aggregator->m_latest_worker[p_src] = p_frame;
                    m_aggregator->m_last_seen_ms[p_src] = v_ts;
                    v_received = true;
                },
                [this, &v_received](const std::uint32_t p_src, const node_frame& p_frame) {
                    const std::scoped_lock v_lock(m_aggregator->m_mtx);
                    m_aggregator->m_latest_node[p_src] = p_frame;
                    v_received = true;
                });
        return v_received;
    }

    void telemetry_hub::snapshot_worker_frame(worker_frame& p_out) noexcept {
        std::memset(&p_out, 0, sizeof(worker_frame));
        p_out.m_version = TELEMETRY_SCHEMA_VERSION;
        p_out.m_worker_id = m_worker_id;
        p_out.m_seq_no = m_seq_no.load(std::memory_order_relaxed);
        p_out.m_worker_local_ms = monotonic_ms();

        p_out.m_tasks_started_total = m_counters->m_tasks_started_total.load(std::memory_order_relaxed);
        p_out.m_tasks_sent_total = m_counters->m_tasks_sent_total.load(std::memory_order_relaxed);
        p_out.m_tasks_recv_total = m_counters->m_tasks_recv_total.load(std::memory_order_relaxed);
        p_out.m_bytes_sent_total = m_counters->m_bytes_sent_total.load(std::memory_order_relaxed);
        p_out.m_bytes_recv_total = m_counters->m_bytes_recv_total.load(std::memory_order_relaxed);
        p_out.m_send_microseconds_total = m_counters->m_send_nanoseconds_total.load(std::memory_order_relaxed) / 1000;
        p_out.m_recv_microseconds_total = m_counters->m_recv_nanoseconds_total.load(std::memory_order_relaxed) / 1000;
        p_out.m_center_state_msgs_total = m_counters->m_center_state_msgs_total.load(std::memory_order_relaxed);
        p_out.m_center_score_proposals_total = m_counters->m_center_score_proposals_total.load(std::memory_order_relaxed);
        p_out.m_center_score_broadcasts_total = m_counters->m_center_score_broadcasts_total.load(std::memory_order_relaxed);
        p_out.m_score_proposals_sent = m_counters->m_score_proposals_sent.load(std::memory_order_relaxed);
        p_out.m_telemetry_microseconds_total = m_counters->m_telemetry_microseconds_total.load(std::memory_order_relaxed);

        for (unsigned v_i = 0; v_i < MAX_WORKERS; ++v_i) {
            p_out.m_edges_out[v_i].m_bytes = m_counters->m_edges_out_bytes[v_i].load(std::memory_order_relaxed);
            p_out.m_edges_out[v_i].m_count = m_counters->m_edges_out_count[v_i].load(std::memory_order_relaxed);
        }

        {
            const std::scoped_lock v_lock(m_counters->m_scheduler_mtx);
            m_counters->m_scheduler_clock.fill(p_out, monotonic_us());
        }

        m_process_probe->sample(p_out);

        if (auto* v_lb = gempba::get_load_balancer()) {
            p_out.m_process_threads = v_lb->get_thread_pool_size() + 1;
            p_out.m_idle_microseconds_per_worker = static_cast<std::uint64_t>(v_lb->get_idle_time() * 1'000'000.0);
            p_out.m_idle_microseconds_starved = static_cast<std::uint64_t>(v_lb->get_starved_idle_time() * 1'000'000.0);
            p_out.m_idle_microseconds_imbalanced = static_cast<std::uint64_t>(v_lb->get_imbalanced_idle_time() * 1'000'000.0);
            p_out.m_tasks_running = static_cast<std::uint32_t>(v_lb->get_tasks_running_count());
            p_out.m_spawn_rejected_pool_full = v_lb->get_spawn_rejected_pool_full_count();
            p_out.m_spawn_remote_rejected = v_lb->get_spawn_remote_rejected_count();
        }

#if GEMPBA_MULTIPROCESSING
        if (auto* v_sched = gempba::try_get_scheduler()) {
            p_out.m_scheduler_pending_count = static_cast<std::uint32_t>(v_sched->get_pending_request_count());
        }
#endif
    }

    void telemetry_hub::snapshot_node_frame(node_frame& p_out) noexcept {
        std::memset(&p_out, 0, sizeof(node_frame));
        p_out.m_version = TELEMETRY_SCHEMA_VERSION;
        p_out.m_sentinel_worker_id = m_worker_id;
        p_out.m_sentinel_local_ms = monotonic_ms();
        m_node_probe->sample(p_out);
    }

    std::optional<worker_frame> telemetry_hub::latest_worker_frame(const std::uint32_t p_worker_id) const {
        if (!m_aggregator)
            return std::nullopt;
        const std::scoped_lock v_lock(m_aggregator->m_mtx);
        const auto v_it = m_aggregator->m_latest_worker.find(p_worker_id);
        if (v_it == m_aggregator->m_latest_worker.end())
            return std::nullopt;
        return v_it->second;
    }

    std::optional<node_frame> telemetry_hub::latest_node_frame(const std::uint32_t p_sentinel_worker_id) const {
        if (!m_aggregator)
            return std::nullopt;
        const std::scoped_lock v_lock(m_aggregator->m_mtx);
        const auto v_it = m_aggregator->m_latest_node.find(p_sentinel_worker_id);
        if (v_it == m_aggregator->m_latest_node.end())
            return std::nullopt;
        return v_it->second;
    }

    const topology_snapshot& telemetry_hub::current_topology() const noexcept { return m_topology; }

    const std::string& telemetry_hub::run_id() const noexcept { return m_run_id; }

    bool telemetry_hub::annotate_run(const std::string_view p_key, const std::string_view p_value) {
        const std::string_view v_shown_key = p_key.substr(0, MAX_RUN_ANNOTATION_KEY_BYTES);
        if (p_key.empty() || p_key.size() > MAX_RUN_ANNOTATION_KEY_BYTES || p_value.size() > MAX_RUN_ANNOTATION_VALUE_BYTES) {
            spdlog::warn("telemetry: dropped run annotation '{}': keys take 1 to {} bytes and values up to {}", v_shown_key, MAX_RUN_ANNOTATION_KEY_BYTES, MAX_RUN_ANNOTATION_VALUE_BYTES);
            return false;
        }
        const std::scoped_lock v_lock(m_run_mtx);
        for (auto& [v_key, v_value]: m_run_annotations) {
            if (v_key == p_key) {
                v_value = p_value;
                return true;
            }
        }
        if (m_run_annotations.size() >= MAX_RUN_ANNOTATIONS) {
            spdlog::warn("telemetry: dropped run annotation '{}': a run keeps at most {} annotations", v_shown_key, MAX_RUN_ANNOTATIONS);
            return false;
        }
        m_run_annotations.emplace_back(p_key, p_value);
        return true;
    }

    std::vector<std::pair<std::string, std::string>> telemetry_hub::run_annotations() const {
        const std::scoped_lock v_lock(m_run_mtx);
        return m_run_annotations;
    }

    void telemetry_hub::describe_scheduler(const std::string_view p_name) {
        const std::scoped_lock v_lock(m_run_mtx);
        m_scheduler_name = p_name;
    }

    void telemetry_hub::describe_load_balancer(const balancing_policy p_policy) {
        const std::scoped_lock v_lock(m_run_mtx);
        switch (p_policy) {
            case QUASI_HORIZONTAL:
                m_load_balancer_name = "quasi_horizontal";
                break;
            case WORK_STEALING:
                m_load_balancer_name = "work_stealing";
                break;
        }
    }

    run_description telemetry_hub::describe_run() const {
        run_description v_run;
        v_run.m_mode = m_mode;
        v_run.m_world_size = m_world_size;
        v_run.m_gempba_version = GEMPBA_VERSION;
        {
            const std::scoped_lock v_lock(m_run_mtx);
            v_run.m_scheduler = m_scheduler_name;
            v_run.m_load_balancer = m_load_balancer_name;
            v_run.m_annotations = m_run_annotations;
        }
        if (m_aggregator) {
            const std::scoped_lock v_lock(m_aggregator->m_mtx);
            for (const auto& [v_id, v_frame]: m_aggregator->m_latest_worker) {
                const bool v_is_center = m_mode == runtime_mode::MP_MPI && v_id == 0;
                if (!v_is_center && v_frame.m_process_threads > 0) {
                    v_run.m_threads_per_process = std::max(v_run.m_threads_per_process, v_frame.m_process_threads - 1);
                }
            }
        }
        return v_run;
    }

    std::uint64_t telemetry_hub::program_elapsed_seconds() const noexcept {
        const std::uint64_t v_start = m_start_ms.load(std::memory_order_acquire);
        if (v_start == 0)
            return 0;
        const std::uint64_t v_now = monotonic_ms();
        return v_now > v_start ? (v_now - v_start) / 1000ULL : 0;
    }

    void telemetry_hub::set_worker_interval_ms(const std::uint32_t p_interval_ms) noexcept { m_worker_interval_ms.store(p_interval_ms, std::memory_order_relaxed); }

    void telemetry_hub::mark_as_node_sentinel() noexcept { m_is_sentinel.store(true, std::memory_order_relaxed); }

    std::uint32_t telemetry_hub::worker_interval_ms() const noexcept { return m_worker_interval_ms.load(std::memory_order_relaxed); }

    void telemetry_hub::apply_control_from_client(const control_kind p_kind, const std::uint32_t p_value) noexcept {
        constexpr std::uint32_t k_min_interval_ms = 50;
        constexpr std::uint32_t k_max_interval_ms = 600'000;

        if (p_kind != control_kind::SET_WORKER_INTERVAL_MS) {
            return;
        }
        const std::uint32_t v_value = std::clamp(p_value, k_min_interval_ms, k_max_interval_ms);
        m_worker_interval_ms.store(v_value, std::memory_order_relaxed);

        const std::scoped_lock v_lock(m_pending_fanout_mtx);
        m_pending_fanout.push_back(control_msg{TELEMETRY_SCHEMA_VERSION, p_kind, v_value});
    }

    telemetry_hub* get() noexcept { return g_owner.get(); }

    bool annotate_run(const std::string_view p_key, const std::string_view p_value) {
        if (auto* v_hub = get()) {
            return v_hub->annotate_run(p_key, p_value);
        }
        if (g_enabled) {
            spdlog::warn("telemetry: dropped run annotation '{}': no telemetry hub is installed yet", p_key.substr(0, MAX_RUN_ANNOTATION_KEY_BYTES));
        }
        return false;
    }

    void install(std::unique_ptr<telemetry_hub> p_hub) noexcept {
        if (!g_enabled) {
            return;
        }
        g_owner = std::move(p_hub);
    }

    void uninstall() noexcept {
        if (g_owner) {
            g_owner->shutdown();
        }
        g_owner.reset();
    }

    void disable() noexcept {
        g_enabled = false;
        uninstall();
    }

    void enable() noexcept { g_enabled = true; }

    bool is_enabled() noexcept { return g_enabled; }

} // namespace gempba::telemetry
