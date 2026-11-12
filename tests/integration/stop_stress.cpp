#include "leaf_count.hpp"

#include <gempba/gempba.hpp>
#include <mpi.h>

#include <array>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/**
 * Stops a leaf count too deep to finish: after a delay, one rank (or every rank) requests the stop, by code or by raising
 * SIGTERM. Every rank must return the same interrupted outcome, with the trigger that rank requested, and the root must hold
 * a best result. A stop that doesn't reach every rank hangs.
 *
 * Usage: stop_stress <semi_centralized|centralized> <code|signal> <stopping rank, or -1 for every rank> <delay in ms>
 */

int main(const int p_argc, char* p_argv[]) {
    if (p_argc != 5) {
        std::cerr << "usage: stop_stress <semi_centralized|centralized> <code|signal> <stopping rank|-1> <delay_ms>\n";
        return 2;
    }
    constexpr int MAX_DEPTH = 40;
    const std::string_view v_topology_name = p_argv[1];
    const bool v_by_signal = std::string_view(p_argv[2]) == "signal";
    const int v_stopping_rank = std::stoi(p_argv[3]);
    const auto v_delay = std::chrono::milliseconds(std::stoi(p_argv[4]));

    gempba::scheduler* v_scheduler = gempba::mpi::create_scheduler(leaf_count::parse_topology(v_topology_name));
    const int v_rank = v_scheduler->rank_me();

    std::jthread v_stopper;
    if (v_stopping_rank < 0 || v_stopping_rank == v_rank) {
        v_stopper = std::jthread([v_by_signal, v_delay] {
            std::this_thread::sleep_for(v_delay);
            if (v_by_signal) {
                std::raise(SIGTERM);
            } else {
                gempba::request_stop();
            }
        });
    }

    const auto v_start = std::chrono::steady_clock::now();
    gempba::run_outcome v_outcome;
    const auto v_counter = leaf_count::run(*v_scheduler, MAX_DEPTH, &v_outcome);
    const auto v_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - v_start);
    v_stopper = {};
    v_scheduler->synchronize_stats();

    const std::array<int, 3> v_local = {static_cast<int>(v_outcome.m_reason), static_cast<int>(v_outcome.m_trigger), v_outcome.m_signal};
    std::vector<int> v_all(3 * static_cast<std::size_t>(v_scheduler->world_size()));
    MPI_Gather(v_local.data(), 3, MPI_INT, v_all.data(), 3, MPI_INT, 0, MPI_COMM_WORLD);

    int v_exit_code = 0;
    if (v_rank == 0) {
        const gempba::run_outcome v_expected{gempba::end_reason::INTERRUPTED, v_by_signal ? gempba::stop_trigger::SIGNAL : gempba::stop_trigger::CODE, v_by_signal ? SIGTERM : 0};
        for (int v_other = 0; v_other < v_scheduler->world_size(); ++v_other) {
            const int* v_values = &v_all[3 * static_cast<std::size_t>(v_other)];
            const gempba::run_outcome v_seen{static_cast<gempba::end_reason>(v_values[0]), static_cast<gempba::stop_trigger>(v_values[1]), v_values[2]};
            if (v_seen != v_expected) {
                std::cerr << "rank " << v_other << " returned reason " << v_values[0] << ", trigger " << v_values[1] << ", signal " << v_values[2] << '\n';
                v_exit_code = 1;
            }
        }
        const std::optional<int> v_best = leaf_count::deserialize_leaf_index(v_scheduler->get_result());
        const std::size_t v_stats = v_scheduler->get_stats_vector().size();
        std::cout << v_topology_name << " ranks=" << v_scheduler->world_size() << " trigger=" << p_argv[2] << " stopping_rank=" << v_stopping_rank << " elapsed_ms=" << v_elapsed.count()
                  << " best=" << v_best.value_or(-1) << " stats=" << v_stats << '\n';
        if (!v_best.has_value() || v_stats != static_cast<std::size_t>(v_scheduler->world_size()) || v_elapsed > v_delay + std::chrono::seconds(10)) {
            v_exit_code = 1;
        }
    }
    v_scheduler->barrier();
    const int v_shutdown_code = gempba::shutdown();
    return v_exit_code != 0 ? v_exit_code : v_shutdown_code;
}
