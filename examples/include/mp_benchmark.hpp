#ifndef GEMPBA_MP_BENCHMARK_H
#define GEMPBA_MP_BENCHMARK_H
#include "benchmark_utils.hpp"
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>

#include <gempba/gempba.hpp>

// --- Configuration ----------------------------------------------------------
constexpr int INTERNAL_COST_US = 80; // Cost per internal node (argument prep)
constexpr int LEAF_COST_US = 5; // Light cost at the leaf
constexpr bool RANDOMIZE_COST = true; // Add jitter to cost simulation
constexpr int ARG_SIZE = 256; // Size of simulated argument (bytes)
constexpr unsigned RNG_SEED = 12345; // Fixed for reproducibility
// ---------------------------------------------------------------------------
std::mt19937 rng(RNG_SEED);
std::uniform_int_distribution<int> jitter(-30, 30);

class mp_benchmark {

    gempba::node_manager &m_node_manager;
    gempba::load_balancer *m_load_balancer;
    std::function<void(std::thread::id, int, int, std::vector<double>, gempba::node)> m_explore_func;
    std::function<gempba::task_packet(int, int, std::vector<double>)> m_args_serializer;
    std::function<std::tuple<int, int, std::vector<double> >(gempba::task_packet)> m_args_deserializer;

public:
    mp_benchmark(gempba::node_manager &p_node_manager, gempba::load_balancer *p_load_balancer) :
        m_node_manager(p_node_manager), m_load_balancer(p_load_balancer) {
        m_explore_func = std::bind(&mp_benchmark::explore, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);
        m_args_serializer = make_serializer();
        m_args_deserializer = make_deserializer();
    }

private:
    // Simulate CPU work (busy wait)
    static void busy_for_microseconds(int p_microseconds) {
        if (p_microseconds <= 0)
            return;
        auto v_start = std::chrono::high_resolution_clock::now();
        while (std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - v_start).count() < p_microseconds) {
            asm volatile("" ::: "memory");
        }
    }

    // Simulated "argument construction" — mimics creating new states
    static void simulate_argument_build(std::vector<double> &p_arg, const int p_depth) {
        // Small computation that scales with ARG_SIZE and depth
        for (auto &v_value: p_arg) {
            v_value = std::sin(v_value + p_depth * 0.001);
        }
    }

public:
    // Recursive traversal that simulates realistic branching cost
    void explore(std::thread::id p_tid, const int p_depth, int p_max_depth, std::vector<double> p_arg, gempba::node p_parent) {
        if (p_depth == p_max_depth) {
            // Minimal work at leaf
            busy_for_microseconds(LEAF_COST_US);
            return;
        }

        // --- Realistic per-node work: argument building ---
        int v_cost = INTERNAL_COST_US;
        if (RANDOMIZE_COST)
            v_cost = std::max(1, INTERNAL_COST_US + jitter(rng));

        simulate_argument_build(p_arg, p_depth);
        busy_for_microseconds(v_cost);


        gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(*m_load_balancer) : p_parent;


        // --- Recursive branching ---
        std::vector<double> v_left_arg = p_arg; // Create argument for left branch
        std::vector<double> v_right_arg = p_arg; // Create argument for right branch

        // Perturb arguments slightly to simulate different branch states
        v_left_arg[p_depth % ARG_SIZE] += 0.001;
        v_right_arg[p_depth % ARG_SIZE] -= 0.001;

        gempba::node v_left = gempba::mp::create_explicit_node<void, int, int, std::vector<double> >(*m_load_balancer,
                                                                                                     v_parent,
                                                                                                     m_explore_func,
                                                                                                     std::make_tuple(p_depth + 1, p_max_depth, std::move(v_left_arg)),
                                                                                                     m_args_serializer,
                                                                                                     m_args_deserializer);

        gempba::node v_right = gempba::mp::create_explicit_node<void, int, int, std::vector<double> >(*m_load_balancer,
                                                                                                      v_parent,
                                                                                                      m_explore_func,
                                                                                                      std::make_tuple(p_depth + 1, p_max_depth, std::move(v_right_arg)),
                                                                                                      m_args_serializer,
                                                                                                      m_args_deserializer);

        bool v_left_submitted = m_node_manager.try_remote_submit(v_left, 0);
        m_node_manager.forward(v_right);
    }
};

#endif //GEMPBA_MP_BENCHMARK_H
