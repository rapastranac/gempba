#ifndef GEMPBA_TESTS_INTEGRATION_LEAF_COUNT_HPP
#define GEMPBA_TESTS_INTEGRATION_LEAF_COUNT_HPP

#include <gempba/gempba.hpp>
#include <gempba/mpi.hpp>

#include <atomic>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <thread>
#include <tuple>

/**
 * Counts the leaves of a full binary tree across all ranks with many tiny tasks. Each leaf's score is its
 * rank-local visit index, so the best score keeps improving and the center keeps broadcasting score updates
 * until the search ends.
 */
namespace leaf_count {

    inline gempba::task_packet serialize(const int p_depth, const int p_max_depth) {
        const int v_values[2] = {p_depth, p_max_depth};
        return {reinterpret_cast<const char*>(v_values), sizeof(v_values)};
    }

    inline std::tuple<int, int> deserialize(const gempba::task_packet& p_packet) {
        int v_values[2];
        std::memcpy(v_values, p_packet.data(), sizeof(v_values));
        return {v_values[0], v_values[1]};
    }

    class leaf_counter {
    public:
        leaf_counter(gempba::load_balancer& p_load_balancer, gempba::node_manager& p_node_manager) : m_load_balancer(p_load_balancer), m_node_manager(p_node_manager) {}

        [[nodiscard]] const std::function<void(std::thread::id, int, int, gempba::node)>& function() const { return m_explore; }

        [[nodiscard]] long long leaves() const { return m_leaves.load(); }

    private:
        void explore(const int p_depth, const int p_max_depth, const gempba::node& p_parent) {
            if (p_depth == p_max_depth) {
                const long long v_leaf_index = m_leaves.fetch_add(1, std::memory_order_relaxed);
                m_node_manager.try_update_score_and_invalidate_result(gempba::score::make(static_cast<int>(v_leaf_index)));
                return;
            }
            gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(m_load_balancer) : p_parent;
            gempba::node v_left = gempba::create_explicit_node<void, int, int>(m_load_balancer, v_parent, m_explore, std::make_tuple(p_depth + 1, p_max_depth), m_serializer, m_deserializer);
            gempba::node v_right =
                    gempba::create_explicit_node<void, int, int>(m_load_balancer, v_parent, m_explore, std::make_tuple(p_depth + 1, p_max_depth), m_serializer, m_deserializer);
            m_node_manager.try_remote_submit(v_left, 0);
            m_node_manager.forward(v_right);
        }

        gempba::load_balancer& m_load_balancer;
        gempba::node_manager& m_node_manager;
        std::atomic<long long> m_leaves{0};
        std::function<void(std::thread::id, int, int, gempba::node)> m_explore = [this](std::thread::id, const int p_depth, const int p_max_depth, const gempba::node& p_parent) {
            explore(p_depth, p_max_depth, p_parent);
        };
        std::function<gempba::task_packet(int, int)> m_serializer = serialize;
        std::function<std::tuple<int, int>(gempba::task_packet)> m_deserializer = [](const gempba::task_packet& p_packet) { return deserialize(p_packet); };
    };

    inline gempba::mpi::scheduler_topology parse_topology(const std::string_view p_name) {
        return p_name == "centralized" ? gempba::mpi::scheduler_topology::CENTRALIZED : gempba::mpi::scheduler_topology::SEMI_CENTRALIZED;
    }

    /**
     * Creates the load balancer and node manager for @p p_scheduler and counts the leaves of a tree of depth @p p_max_depth,
     * seeded from the root, with two threads per rank that runs tasks. Returns once this rank's run ends; keep the counter until shutdown.
     */
    inline std::unique_ptr<leaf_counter> run(gempba::scheduler& p_scheduler, const int p_max_depth) {
        gempba::load_balancer* v_load_balancer = gempba::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL, &p_scheduler);
        gempba::node_manager& v_node_manager = gempba::create_node_manager(v_load_balancer, &p_scheduler);
        v_node_manager.set_goal(gempba::MAXIMISE, gempba::score_type::I32);
        p_scheduler.set_goal(gempba::MAXIMISE, gempba::score_type::I32);
        v_node_manager.set_score(gempba::score::make(0));
        auto v_counter = std::make_unique<leaf_counter>(*v_load_balancer, v_node_manager);
        p_scheduler.barrier();

        if (!p_scheduler.is_root()) {
            v_node_manager.set_thread_pool_size(2);
        }
        const std::function<std::tuple<int, int>(const gempba::task_packet&&)> v_deserializer = [](const gempba::task_packet&& p_packet) { return deserialize(p_packet); };
        const auto v_runnable = gempba::runnables::return_none::create(0, v_counter->function(), v_deserializer);
        const std::map<int, std::shared_ptr<gempba::serial_runnable>> v_runnables{{v_runnable->get_id(), v_runnable}};
        std::optional<gempba::task_bundle> v_seed;
        if (p_scheduler.is_root()) {
            v_seed.emplace(serialize(0, p_max_depth), 0);
        }
        p_scheduler.run(v_node_manager, v_runnables, std::move(v_seed));
        return v_counter;
    }

} // namespace leaf_count

#endif // GEMPBA_TESTS_INTEGRATION_LEAF_COUNT_HPP
