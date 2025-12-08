#include "include/benchmark.hpp"
#include "include/mt_benchmark.hpp"
#include <gempba/gempba.hpp>

#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>

using namespace std::chrono;

int run(const benchmark_params &p_params) {

    // logging in center only
    std::cout << "USING OPTIMIZED ENCODING" << std::endl;
    std::cout << "USING SEMI-CENTRALIZED STRATEGY" << std::endl << std::endl;
    std::cout << "Running with parameters: " << std::endl;
    std::cout << "Job name : " << p_params.job_name << std::endl;
    std::cout << "Job id : " << p_params.job_id << std::endl;
    std::cout << "Nodes : " << p_params.nodes << std::endl;
    std::cout << "NTasks per node : " << p_params.ntasks_per_node << std::endl;
    std::cout << "NTasks per socket : " << p_params.ntasks_per_socket << std::endl;
    std::cout << "Threads per task : " << p_params.cpus_per_task << std::endl;
    std::cout << "Directory name : " << p_params.dir_name << std::endl;


    gempba::load_balancer *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::node_manager &v_node_manager = gempba::mt::create_node_manager(v_load_balancer);
    v_node_manager.set_goal(gempba::MAXIMISE, gempba::score_type::I32);
    v_node_manager.set_thread_pool_size(p_params.cpus_per_task);
    v_node_manager.set_score(gempba::score::make(0)); // arbitrary initial score

    mt_benchmark v_instance(v_node_manager, v_load_balancer);

    auto v_function = std::bind(&mt_benchmark::explore, &v_instance, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);

    // target algorithm [all arguments]

    std::cout << "Running realistic brute-force binary traversal:\n"
            << "  Depth = " << p_params.max_depth
            << " (leaves = " << (1ULL << p_params.max_depth) << ")\n";

    std::vector<double> v_initial_arg(ARG_SIZE);
    std::iota(v_initial_arg.begin(), v_initial_arg.end(), 0.0);

    gempba::node v_seed = gempba::create_seed_node<void, int, int, std::vector<double> >(*v_load_balancer,
                                                                                         v_function,
                                                                                         std::make_tuple(0, p_params.max_depth, v_initial_arg));

    std::this_thread::sleep_for(100ms); // allow logging to settle
    const double v_start_time = v_node_manager.get_wall_time();
    // explore(0, MAX_DEPTH, initialArg);
    const bool v_submitted = v_node_manager.try_local_submit(v_seed);
    if (!v_submitted) {
        throw std::runtime_error("unable to submit seed node");
    }
    v_node_manager.wait();
    const double v_end_time = v_node_manager.get_wall_time();

    const gempba::score v_score = v_node_manager.get_score();
    double v_elapsed_time = v_end_time - v_start_time;
    double v_global_idle_time = v_node_manager.get_idle_time();
    size_t v_thread_request_count = v_node_manager.get_thread_request_count();

    spdlog::info("Score: {}", v_score.to_string());
    spdlog::info("global pool idle time: {0:.6f} seconds", v_global_idle_time);
    spdlog::info("elapsed time: {0:.6f}", v_elapsed_time);
    spdlog::info("thread requests: {}", v_thread_request_count);

    // print stats to a file ***********
    print_to_summary_file(p_params,
                          -1,
                          v_elapsed_time,
                          v_global_idle_time,
                          v_thread_request_count,
                          0,
                          {0},
                          {0},
                          {0}
            );


    return gempba::shutdown();
}

int main(const int argc, char *argv[]) {
    const benchmark_params v_params = parse(argc, argv);
    return run(v_params);
}
