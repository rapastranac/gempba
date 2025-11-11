#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <spdlog/spdlog.h>

#include "include/main.hpp"
#include "include/mt_bitvector_optimized_encoding.hpp"

using namespace std::placeholders;

int run(const std::string& p_job_name, int p_job_id, int p_nodes, int p_ntasks_per_node, int p_ntasks_per_socket, int p_threads_per_task, int p_probability, bool p_csv_append, const std::string &p_filename_directory) {

    std::cout << "USING OPTIMIZED ENCODING" << std::endl;
    std::cout << "USING MULTITHREADING ONLY" << std::endl;
    std::cout << "NUMTHREADS= " << p_threads_per_task << std::endl;

    gempba::load_balancer *v_load_balancer = gempba::mt::create_load_balancer(gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::node_manager &v_node_manager = gempba::mt::create_node_manager(v_load_balancer);

    v_node_manager.set_goal(gempba::MINIMISE, gempba::score_type::I32);
    v_node_manager.set_thread_pool_size(p_threads_per_task);

    mt_bitvector_optimized_encoding v_instance(v_node_manager, v_load_balancer);
    auto v_function = std::bind(&mt_bitvector_optimized_encoding::mvcbitset, &v_instance, _1, _2, _3, _4, _5); // target algorithm [all arguments]


    Graph v_graph;
    v_graph.readEdges(p_filename_directory);

    v_instance.init(v_graph, p_threads_per_task, p_filename_directory, p_probability);
    v_instance.set_graph(v_graph);

    int v_gsize = v_graph.size();
    GBITSET v_allzeros(v_gsize);
    GBITSET v_allones = ~v_allzeros;

    v_node_manager.set_score(gempba::score::make(v_gsize)); // thus, all processes know the best value so far
    v_node_manager.set_goal(gempba::MINIMISE, gempba::score_type::I32);

    const int v_zero = 0;
    const int v_solution_size = v_graph.size();
    std::cout << "solsize=" << v_solution_size << std::endl;

    gempba::node v_dummy = gempba::create_dummy_node(*v_load_balancer);

    const std::tuple<int, GBITSET, int> v_seed_args = std::make_tuple(v_zero, v_allones, v_zero);
    gempba::node v_seed = gempba::create_seed_node<void>(*v_load_balancer, v_function, v_seed_args);

    const double v_start_time = gempba::node_manager::get_wall_time();
    const bool v_submitted = v_node_manager.try_local_submit(v_seed);
    if (!v_submitted) {
        throw std::runtime_error("unable to submit seed node");
    }
    v_node_manager.wait();
    const double v_end_time = gempba::node_manager::get_wall_time();

    const gempba::score v_score = v_node_manager.get_score();
    double v_elapsed_time = v_end_time - v_start_time;
    double v_global_idle_time = v_node_manager.get_idle_time();
    size_t v_thread_request_count = v_node_manager.get_thread_request_count();

    spdlog::info("cover size: {}", v_score.to_string());
    spdlog::info("global pool idle time: {0:.6f} seconds", v_global_idle_time);
    spdlog::info("elapsed time: {0:.6f}", v_elapsed_time);
    spdlog::info("thread requests: {}", v_thread_request_count);

    // print stats to a file ***********
    print_to_summary_file(p_job_name, p_job_id, p_nodes, p_ntasks_per_node, p_ntasks_per_socket, p_threads_per_task,
                          p_filename_directory, v_elapsed_time, v_gsize, -1, {},
                          {}, {}, v_score.get_loose<int>(), v_global_idle_time,
                          v_thread_request_count, 0, p_csv_append);

    return gempba::shutdown();
}

int main(const int argc, char *argv[]) {
    const auto [job_name, job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob,csv_append, filename] = parse(argc, argv);

    return run(job_name, job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob,csv_append, filename);
}
