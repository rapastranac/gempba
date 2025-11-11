#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <spdlog/spdlog.h>

#include "include/main.hpp"
#include "include/mt_bitvector_optimized_encoding.hpp"

using namespace std::placeholders;

int run(const int p_threads_per_task, const int p_probability, const std::string &p_filename_path) {

    std::cout << "USING OPTIMIZED ENCODING" << std::endl;
    std::cout << "USING MULTITHREADING ONLY" << std::endl;
    std::cout << "NUMTHREADS= " << p_threads_per_task << std::endl;

    gempba::load_balancer *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::branch_handler &v_branch_handler = gempba::mt::create_branch_handler(v_load_balancer);

    v_branch_handler.set_goal(gempba::MINIMISE, gempba::score_type::I32);
    v_branch_handler.set_thread_pool_size(p_threads_per_task);

    mt_bitvector_optimized_encoding v_instance(v_branch_handler, v_load_balancer);
    auto v_function = std::bind(&mt_bitvector_optimized_encoding::mvcbitset, &v_instance, _1, _2, _3, _4, _5); // target algorithm [all arguments]


    Graph v_graph;
    v_graph.readEdges(p_filename_path);

    v_instance.init(v_graph, p_threads_per_task, p_filename_path, p_probability);
    v_instance.set_graph(v_graph);

    int v_gsize = v_graph.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
    GBITSET v_allzeros(v_gsize);
    GBITSET v_allones = ~v_allzeros;

    v_branch_handler.set_score(gempba::score::make(v_gsize)); // thus, all processes know the best value so far
    v_branch_handler.set_goal(gempba::MINIMISE, gempba::score_type::I32);

    const int v_zero = 0;
    const int v_solution_size = v_graph.size();
    std::cout << "solsize=" << v_solution_size << std::endl;

    gempba::node v_dummy = gempba::node_factory::create_dummy_node(*v_load_balancer);

    const std::tuple<int, GBITSET, int> v_seed_args = std::make_tuple(v_zero, v_allones, v_zero);
    gempba::node v_seed = gempba::node_factory::create_seed_node<void>(*v_load_balancer, v_function, v_seed_args);

    const double v_start_time = gempba::branch_handler::get_wall_time();
    const bool v_submitted = v_branch_handler.try_local_submit(v_seed);
    if (!v_submitted) {
        throw std::runtime_error("unable to submit seed node");
    }
    v_branch_handler.wait2();
    const double v_end_time = gempba::branch_handler::get_wall_time();

    const gempba::score v_score = v_branch_handler.get_score();
    double v_elapsed_time = v_end_time - v_start_time;
    double v_global_idle_time = v_branch_handler.get_idle_time();
    size_t v_thread_request_count = v_branch_handler.get_thread_request_count();

    spdlog::info("cover size: {}", v_score.to_string());
    spdlog::info("global pool idle time: {0:.6f} seconds", v_global_idle_time);
    spdlog::info("elapsed time: {0:.6f}", v_elapsed_time);
    spdlog::info("thread requests: {}", v_thread_request_count);

    return EXIT_SUCCESS;
}

int main(const int argc, char *argv[]) {
    const auto [job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, filename] = parse(argc, argv);

    return run(cpus_per_task, prob, filename);
}
