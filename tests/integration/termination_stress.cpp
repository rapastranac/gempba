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

#include "leaf_count.hpp"

#include <gempba/gempba.hpp>
#include <mpi.h>

#include <algorithm>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

/**
 * Runs the leaf count with many tiny tasks, so that termination races the hand-offs. A premature termination
 * loses subtrees (wrong count), aborts on a stray message, or hangs. A lost score proposal leaves rank 0 without
 * the best result: the highest visit index any rank reached.
 *
 * Usage: termination_stress <depth> <semi_centralized|centralized>
 */

int main(const int p_argc, char* p_argv[]) {
    if (p_argc != 3) {
        std::cerr << "usage: termination_stress <depth> <semi_centralized|centralized>\n";
        return 2;
    }
    const int v_max_depth = std::stoi(p_argv[1]);
    const std::string_view v_topology_name = p_argv[2];

    gempba::scheduler* v_scheduler = gempba::mpi::create_scheduler(leaf_count::parse_topology(v_topology_name));
    const int v_rank = v_scheduler->rank_me();
    gempba::run_outcome v_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::NONE, 0};
    const auto v_counter = leaf_count::run(*v_scheduler, v_max_depth, &v_outcome);
    v_scheduler->barrier();

    const long long v_local = v_counter->leaves();
    std::vector<long long> v_per_rank(static_cast<std::size_t>(v_scheduler->world_size()));
    MPI_Gather(&v_local, 1, MPI_LONG_LONG, v_per_rank.data(), 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);

    int v_exit_code = 0;
    if (v_outcome != gempba::run_outcome{}) {
        std::cerr << "rank " << v_rank << " says the run didn't complete\n";
        v_exit_code = 1;
    }
    if (v_rank == 0) {
        const long long v_total = std::accumulate(v_per_rank.begin(), v_per_rank.end(), 0LL);
        const long long v_expected = 1LL << v_max_depth;
        const long long v_expected_best = *std::ranges::max_element(v_per_rank) - 1;
        const int v_best = leaf_count::deserialize_leaf_index(v_scheduler->get_result()).value_or(-1);
        std::cout << v_topology_name << " ranks=" << v_scheduler->world_size() << " leaves=" << v_total << " expected=" << v_expected << " best=" << v_best
                  << " expected_best=" << v_expected_best << " per_rank=";
        for (const long long v_count: v_per_rank) {
            std::cout << ' ' << v_count;
        }
        std::cout << '\n';
        if (v_total != v_expected || v_best != v_expected_best) {
            v_exit_code = 1;
        }
    }
    v_scheduler->barrier();
    const int v_shutdown_code = gempba::shutdown();
    return v_exit_code != 0 ? v_exit_code : v_shutdown_code;
}
