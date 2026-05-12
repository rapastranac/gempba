#include <gempba/config.h>
#include <gempba/core/load_balancer.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <impl/telemetry/center_tcp_server.hpp>
#include <impl/telemetry/local_transport.hpp>
#include <impl/telemetry/node_probe.hpp>
#include <impl/telemetry/process_probe.hpp>
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
#include <thread>
#include <utility>
#include <vector>

namespace gempba::telemetry {

    namespace {
        std::unique_ptr<telemetry_hub> g_owner;
        bool g_enabled = true;

        std::uint64_t monotonic_ms() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count());
        }

        constexpr std::chrono::milliseconds k_pump_resolution{50};

        constexpr std::uint16_t k_default_telemetry_port = 9000;
        std::atomic<std::uint16_t> g_configured_port{k_default_telemetry_port};
    } // namespace

    void configure_port(const std::uint16_t p_port) noexcept { g_configured_port.store(p_port, std::memory_order_release); }

    struct telemetry_hub::counters {
        std::atomic<std::uint64_t> m_tasks_local_total{0};
        std::atomic<std::uint64_t> m_tasks_sent_total{0};
        std::atomic<std::uint64_t> m_tasks_recv_total{0};

        std::atomic<std::uint64_t> m_edges_out_bytes[MAX_WORKERS]{};
        std::atomic<std::uint64_t> m_edges_out_count[MAX_WORKERS]{};
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

        // Hold one broadcast window so any connected dashboard receives the
        // trailing publish below before the socket tears down.
        if (m_tcp_server) {
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        }

        // Force a final snapshot so the aggregator (and any consumer reading
        // it) reflects post-wait counters, not the snapshot taken up to one
        // worker_interval_ms before completion.
        if (m_transport) {
            m_seq_no.fetch_add(1, std::memory_order_relaxed);
            worker_frame v_final{};
            snapshot_worker_frame(v_final);
            m_transport->publish_worker(v_final);
            if (m_is_sentinel.load(std::memory_order_relaxed)) {
                node_frame v_node_final{};
                snapshot_node_frame(v_node_final);
                m_transport->publish_node(v_node_final);
            }
            if (m_aggregator) {
                m_transport->poll(
                        [this](const std::uint32_t p_src, const worker_frame& p_frame) {
                            const std::scoped_lock v_lock(m_aggregator->m_mtx);
                            m_aggregator->m_latest_worker[p_src] = p_frame;
                            m_aggregator->m_last_seen_ms[p_src] = monotonic_ms();
                        },
                        [this](const std::uint32_t p_src, const node_frame& p_frame) {
                            const std::scoped_lock v_lock(m_aggregator->m_mtx);
                            m_aggregator->m_latest_node[p_src] = p_frame;
                        });
            }
        }

        if (m_tcp_server) {
            m_tcp_server->stop();
            m_tcp_server.reset();
        }
        m_transport.reset();
        m_aggregator.reset();
    }

    void telemetry_hub::record_send(const std::uint32_t p_dst_worker_id, const std::uint64_t p_bytes) noexcept {
        if (p_dst_worker_id < MAX_WORKERS) {
            m_counters->m_edges_out_bytes[p_dst_worker_id].fetch_add(p_bytes, std::memory_order_relaxed);
            m_counters->m_edges_out_count[p_dst_worker_id].fetch_add(1, std::memory_order_relaxed);
        }
        m_counters->m_tasks_sent_total.fetch_add(1, std::memory_order_relaxed);
    }

    void telemetry_hub::record_recv(std::uint32_t, const std::uint64_t) noexcept { m_counters->m_tasks_recv_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::record_task_local() noexcept { m_counters->m_tasks_local_total.fetch_add(1, std::memory_order_relaxed); }

    void telemetry_hub::tick_if_due() noexcept {
        if (!m_transport)
            return;

        const std::uint64_t v_now = monotonic_ms();
        const std::uint32_t v_worker_interval = m_worker_interval_ms.load(std::memory_order_relaxed);

        if (v_now - m_last_worker_emit_ms.load(std::memory_order_relaxed) >= v_worker_interval) {
            m_seq_no.fetch_add(1, std::memory_order_relaxed);
            worker_frame v_frame{};
            snapshot_worker_frame(v_frame);
            m_transport->publish_worker(v_frame);
            m_last_worker_emit_ms.store(v_now, std::memory_order_relaxed);
        }

        if (m_is_sentinel.load(std::memory_order_relaxed)) {
            const std::uint32_t v_node_interval = m_node_interval_ms.load(std::memory_order_relaxed);
            if (v_now - m_last_node_emit_ms.load(std::memory_order_relaxed) >= v_node_interval) {
                node_frame v_node_frame{};
                snapshot_node_frame(v_node_frame);
                m_transport->publish_node(v_node_frame);
                m_last_node_emit_ms.store(v_now, std::memory_order_relaxed);
            }
        }

        while (auto v_ctrl = m_transport->try_recv_control()) {
            switch (v_ctrl->m_kind) {
                case control_kind::UNSPECIFIED:
                    break;
                case control_kind::SET_WORKER_INTERVAL_MS:
                    m_worker_interval_ms.store(v_ctrl->m_value, std::memory_order_relaxed);
                    break;
                case control_kind::SET_NODE_INTERVAL_MS:
                    m_node_interval_ms.store(v_ctrl->m_value, std::memory_order_relaxed);
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

        if (m_aggregator) {
            const std::uint64_t v_ts = monotonic_ms();
            m_transport->poll(
                    [this, v_ts](const std::uint32_t p_src, const worker_frame& p_frame) {
                        const std::scoped_lock v_lock(m_aggregator->m_mtx);
                        m_aggregator->m_latest_worker[p_src] = p_frame;
                        m_aggregator->m_last_seen_ms[p_src] = v_ts;
                    },
                    [this](const std::uint32_t p_src, const node_frame& p_frame) {
                        const std::scoped_lock v_lock(m_aggregator->m_mtx);
                        m_aggregator->m_latest_node[p_src] = p_frame;
                    });
        }
    }

    void telemetry_hub::snapshot_worker_frame(worker_frame& p_out) noexcept {
        std::memset(&p_out, 0, sizeof(worker_frame));
        p_out.m_version = TELEMETRY_SCHEMA_VERSION;
        p_out.m_worker_id = m_worker_id;
        p_out.m_seq_no = m_seq_no.load(std::memory_order_relaxed);
        p_out.m_worker_local_ms = monotonic_ms();

        p_out.m_tasks_local_total = m_counters->m_tasks_local_total.load(std::memory_order_relaxed);
        p_out.m_tasks_sent_total = m_counters->m_tasks_sent_total.load(std::memory_order_relaxed);
        p_out.m_tasks_recv_total = m_counters->m_tasks_recv_total.load(std::memory_order_relaxed);

        for (unsigned v_i = 0; v_i < MAX_WORKERS; ++v_i) {
            p_out.m_edges_out[v_i].m_bytes = m_counters->m_edges_out_bytes[v_i].load(std::memory_order_relaxed);
            p_out.m_edges_out[v_i].m_count = m_counters->m_edges_out_count[v_i].load(std::memory_order_relaxed);
        }

        m_process_probe->sample(p_out);

        if (auto* v_lb = gempba::get_load_balancer()) {
            p_out.m_process_threads = v_lb->get_thread_pool_size() + 1;
            p_out.m_idle_microseconds_per_worker = static_cast<std::uint64_t>(v_lb->get_idle_time() * 1'000'000.0);
            p_out.m_tasks_running = static_cast<std::uint32_t>(v_lb->get_tasks_running_count());
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

    std::uint64_t telemetry_hub::program_elapsed_seconds() const noexcept {
        const std::uint64_t v_start = m_start_ms.load(std::memory_order_acquire);
        if (v_start == 0)
            return 0;
        const std::uint64_t v_now = monotonic_ms();
        return v_now > v_start ? (v_now - v_start) / 1000ULL : 0;
    }

    void telemetry_hub::set_worker_interval_ms(const std::uint32_t p_interval_ms) noexcept { m_worker_interval_ms.store(p_interval_ms, std::memory_order_relaxed); }

    void telemetry_hub::set_node_interval_ms(const std::uint32_t p_interval_ms) noexcept { m_node_interval_ms.store(p_interval_ms, std::memory_order_relaxed); }

    void telemetry_hub::mark_as_node_sentinel() noexcept { m_is_sentinel.store(true, std::memory_order_relaxed); }

    std::uint32_t telemetry_hub::worker_interval_ms() const noexcept { return m_worker_interval_ms.load(std::memory_order_relaxed); }

    std::uint32_t telemetry_hub::node_interval_ms() const noexcept { return m_node_interval_ms.load(std::memory_order_relaxed); }

    void telemetry_hub::apply_control_from_client(const control_kind p_kind, const std::uint32_t p_value) noexcept {
        constexpr std::uint32_t k_min_interval_ms = 50;
        constexpr std::uint32_t k_max_interval_ms = 600'000;

        std::uint32_t v_value = p_value;
        switch (p_kind) {
            case control_kind::SET_WORKER_INTERVAL_MS:
            case control_kind::SET_NODE_INTERVAL_MS:
                v_value = std::clamp(v_value, k_min_interval_ms, k_max_interval_ms);
                break;
            default:
                return;
        }

        if (p_kind == control_kind::SET_WORKER_INTERVAL_MS) {
            m_worker_interval_ms.store(v_value, std::memory_order_relaxed);
        } else {
            m_node_interval_ms.store(v_value, std::memory_order_relaxed);
        }

        const std::scoped_lock v_lock(m_pending_fanout_mtx);
        m_pending_fanout.push_back(control_msg{TELEMETRY_SCHEMA_VERSION, p_kind, v_value});
    }

    telemetry_hub* get() noexcept { return g_owner.get(); }

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
