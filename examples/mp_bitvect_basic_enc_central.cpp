#include <filesystem>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#include <spdlog/spdlog.h>

#include <gempba/gempba.hpp>

#include "include/main.hpp"
#include "include/mp_bitvector_basic_encoding.hpp"

using namespace std::placeholders;

gempba::node_manager &initiate_node_manager(gempba::scheduler *p_scheduler, gempba::load_balancer *p_load_balancer) {
    if (p_scheduler->rank_me() == 0) {
        return gempba::mp::create_node_manager(p_load_balancer, nullptr);
    }
    gempba::scheduler::worker *v_worker_view = &p_scheduler->worker_view();
    return gempba::mp::create_node_manager(p_load_balancer, v_worker_view);
}

gempba::load_balancer *initiate_load_balancer(gempba::scheduler *p_scheduler, const gempba::balancing_policy p_policy) {
    if (p_scheduler->rank_me() == 0) {
        return gempba::mp::create_load_balancer(p_policy, nullptr);
    }
    gempba::scheduler::worker *v_worker_view = &p_scheduler->worker_view();
    return gempba::mp::create_load_balancer(p_policy, v_worker_view);
}

int run(const std::string& p_job_name, int p_job_id, int p_nodes, int p_ntasks_per_node, int p_ntasks_per_socket, int p_threads_per_task, int p_probability, bool p_csv_append, std::string &p_filename_directory) {

    std::cout << "USING LARGE ENCODING" << std::endl;
    std::cout << "USING CENTRALIZED STRATEGY" << std::endl;

    // NOTE: instantiated object depends on SCHEDULER_CENTRALIZED macro
    auto v_scheduler = gempba::mp::create_scheduler(gempba::mp::scheduler_topology::CENTRALIZED);
    v_scheduler->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int rank = v_scheduler->rank_me();

    std::cout << "NUMTHREADS= " << p_threads_per_task << std::endl;
    gempba::load_balancer *v_load_balancer = initiate_load_balancer(v_scheduler, gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::node_manager &v_node_manager = initiate_node_manager(v_scheduler, v_load_balancer);

    v_node_manager.set_goal(gempba::MINIMISE, gempba::score_type::I32);
    v_scheduler->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    mp_bitvector_basic_encoding cover(v_node_manager, v_load_balancer);
    auto v_function = std::bind(&mp_bitvector_basic_encoding::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]


    // initialize MPI and member variable linkin

    /* this is run by all processes, because it is a bitvector implementation,
        all processes should know the original graph ******************************************************/

    Graph graph;
    graph.readEdges(p_filename_directory);

    cover.init(graph, p_threads_per_task, p_filename_directory, p_probability);
    cover.setGraph(graph);

    int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
    G_BITSET allzeros(gsize);
    G_BITSET allones = ~allzeros;

    v_node_manager.set_score(gempba::score::make(gsize)); // thus, all processes know the best value so far

    int zero = 0;
    int solsize = graph.size();
    std::cout << "solsize=" << solsize << std::endl;
    v_scheduler->barrier();

    BitGraph bg;
    bg.bits_in_graph = allones;
    gempba::task_packet v_buffer = serializer(zero, bg, zero);

    std::cout << "Starting MPI node " << v_scheduler->rank_me() << std::endl;
    v_scheduler->barrier();
    int pid = getpid(); // for debugging purposes
    spdlog::debug("rank {} is process ID : {}\n", rank, pid); // for debugging purposes

    v_scheduler->barrier();

    constexpr int v_runnable_id = 0;
    if (rank == 0) {
        gempba::scheduler::center &v_center_view = v_scheduler->center_view();
        // center process
        const gempba::task_packet& v_seed{v_buffer};
        v_center_view.run(v_seed, v_runnable_id);
    } else {
        // worker process
        gempba::scheduler::worker &v_worker_view = v_scheduler->worker_view();
        v_node_manager.set_thread_pool_size(p_threads_per_task);

        auto v_deser = create_deserializer();
        auto v_runnable = gempba::mp::runnables::return_none::create(v_runnable_id, v_function, v_deser);

        std::map<int, std::shared_ptr<gempba::serial_runnable> > v_runnables;
        v_runnables[v_runnable->get_id()] = v_runnable;

        v_worker_view.run(v_node_manager, v_runnables);
    }
    v_scheduler->barrier();
    // *****************************************************************************************

    auto v_world_size = v_scheduler->world_size();

    // Synchronize stats across all processes
    v_scheduler->synchronize_stats();

    if (rank == 0) {
        // Retrieve solution
        auto &&v_center_view = v_scheduler->center_view();
        gempba::task_packet v_packet = v_center_view.get_result();
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(v_packet.data()), static_cast<int>(v_packet.size()));

        int v_solution_size;
        deserializer(v_ss, v_solution_size);
        spdlog::debug("Cover size : {} \n", v_solution_size);
        spdlog::debug("\n\n\n");

        // Collect and print stats to console ***********
        std::vector<std::unique_ptr<gempba::stats> > v_stats_vector = v_scheduler->get_stats_vector();
        std::vector<std::size_t> v_received_tasks(v_world_size);
        std::vector<std::size_t> v_sent_tasks(v_world_size);
        std::vector<std::size_t> v_total_requests(v_world_size);
        std::vector<std::size_t> v_total_thread_requests(v_world_size);
        std::vector<double> v_idle_times(v_world_size);
        std::vector<double> v_elapsed_times(v_world_size);

        for (int v_rank = 0; v_rank < v_stats_vector.size(); ++v_rank) {
            std::unique_ptr<gempba::stats> &v_stats = v_stats_vector[v_rank];

            std::unique_ptr<gempba::default_mpi_stats_visitor> v_visitor = gempba::mp::get_default_mpi_stats_visitor();
            v_stats->visit(v_visitor.get());

            v_received_tasks[v_rank] = v_visitor->m_received_task_count;
            v_sent_tasks[v_rank] = v_visitor->m_sent_task_count;
            v_total_requests[v_rank] = v_visitor->m_total_requested_tasks;
            v_total_thread_requests[v_rank] = v_visitor->m_total_thread_requests;
            v_idle_times[v_rank] = v_visitor->m_idle_time;
            v_elapsed_times[v_rank] = v_visitor->m_elapsed_time;
        }

        double v_global_cpu_idle_time = 0;
        size_t v_global_thread_request = 0;
        size_t v_total_requests_at_center = v_total_requests[0];
        for (int v_rank = 0; v_rank < v_world_size; ++v_rank) {
            spdlog::info("rank {}", v_rank);
            spdlog::info("  received_task_count: {}", v_received_tasks[v_rank]);
            spdlog::info("  sent_task_count: {}", v_sent_tasks[v_rank]);
            spdlog::info("  total_requested_tasks: {}", v_total_requests[v_rank]);
            spdlog::info("  total_thread_requests: {}", v_total_thread_requests[v_rank]);
            spdlog::info("  idle_time: {}", v_idle_times[v_rank]);
            spdlog::info("  elapsed_time: {}", v_elapsed_times[v_rank]);

            v_global_cpu_idle_time += v_idle_times[v_rank];
            v_global_thread_request += v_total_thread_requests[v_rank];
        }

        // print stats to a file ***********
        print_to_summary_file(p_job_name, p_job_id, p_nodes, p_ntasks_per_node, p_ntasks_per_socket, p_threads_per_task,
                              p_filename_directory, v_elapsed_times[0], gsize, v_world_size, v_total_thread_requests,
                              v_received_tasks, v_sent_tasks, v_solution_size, v_global_cpu_idle_time,
                              v_global_thread_request, v_total_requests_at_center, p_csv_append);
        // **************************************************************************
    }
    return gempba::shutdown();
}


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    std::string job_name = params.job_name;
    int job_id = params.job_id;
    int nodes = params.nodes;
    int ntasks_per_node = params.ntasks_per_node;
    int ntasks_per_socket = params.ntasks_per_socket;
    int prob = params.prob;
    int cpus_per_task = params.cpus_per_task;
    bool csv_append = params.csv_append;
    auto filename = params.filename;

    return run(job_name, job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, csv_append, filename);
}
