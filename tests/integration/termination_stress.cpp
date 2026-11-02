/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <gempba/gempba.hpp>
#include <mpi.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

/**
 * Counts the leaves of a full binary tree across all ranks with many tiny tasks, so that termination races
 * the hand-offs. A premature termination loses subtrees (wrong count), aborts on a stray message, or hangs.
 * Each leaf's score is its rank-local visit index, so the best score keeps improving and the center keeps
 * broadcasting score updates until the search ends.
 *
 * Usage: termination_stress <depth> <semi_centralized|centralized>
 */

namespace {
    gempba::task_packet serialize(const int p_depth, const int p_max_depth) {
        const int v_values[2] = {p_depth, p_max_depth};
        return {reinterpret_cast<const char*>(v_values), sizeof(v_values)};
    }

    std::tuple<int, int> deserialize(const gempba::task_packet& p_packet) {
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
} // namespace

int main(const int p_argc, char* p_argv[]) {
    if (p_argc != 3) {
        std::cerr << "usage: termination_stress <depth> <semi_centralized|centralized>\n";
        return 2;
    }
    const int v_max_depth = std::stoi(p_argv[1]);
    const std::string_view v_topology_name = p_argv[2];
    const auto v_topology = v_topology_name == "centralized" ? gempba::scheduler_topology::CENTRALIZED : gempba::scheduler_topology::SEMI_CENTRALIZED;

    gempba::scheduler* v_scheduler = gempba::create_scheduler(v_topology);
    const int v_rank = v_scheduler->rank_me();
    gempba::scheduler::worker* v_worker = v_rank == 0 ? nullptr : &v_scheduler->worker_view();
    gempba::load_balancer* v_load_balancer = gempba::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL, v_worker);
    gempba::node_manager& v_node_manager = gempba::create_node_manager(v_load_balancer, v_worker);
    v_node_manager.set_goal(gempba::MAXIMISE, gempba::score_type::I32);
    v_scheduler->set_goal(gempba::MAXIMISE, gempba::score_type::I32);
    v_node_manager.set_score(gempba::score::make(0));
    leaf_counter v_counter(*v_load_balancer, v_node_manager);
    v_scheduler->barrier();

    if (v_rank == 0) {
        v_scheduler->center_view().run(serialize(0, v_max_depth), 0);
    } else {
        v_node_manager.set_thread_pool_size(2);
        const std::function<std::tuple<int, int>(const gempba::task_packet&&)> v_deserializer = [](const gempba::task_packet&& p_packet) { return deserialize(p_packet); };
        const auto v_runnable = gempba::runnables::return_none::create(0, v_counter.function(), v_deserializer);
        const std::map<int, std::shared_ptr<gempba::serial_runnable>> v_runnables{{v_runnable->get_id(), v_runnable}};
        v_scheduler->worker_view().run(v_node_manager, v_runnables);
    }
    v_scheduler->barrier();

    const long long v_local = v_counter.leaves();
    std::vector<long long> v_per_rank(static_cast<std::size_t>(v_scheduler->world_size()));
    MPI_Gather(&v_local, 1, MPI_LONG_LONG, v_per_rank.data(), 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);

    int v_exit_code = 0;
    if (v_rank == 0) {
        const long long v_total = std::accumulate(v_per_rank.begin(), v_per_rank.end(), 0LL);
        const long long v_expected = 1LL << v_max_depth;
        std::cout << v_topology_name << " ranks=" << v_scheduler->world_size() << " leaves=" << v_total << " expected=" << v_expected << " per_rank=";
        for (const long long v_count: v_per_rank) {
            std::cout << ' ' << v_count;
        }
        std::cout << '\n';
        if (v_total != v_expected) {
            v_exit_code = 1;
        }
    }
    v_scheduler->barrier();
    const int v_shutdown_code = gempba::shutdown();
    return v_exit_code != 0 ? v_exit_code : v_shutdown_code;
}
