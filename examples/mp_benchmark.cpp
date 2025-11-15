#include "include/benchmark.hpp"
#include "include/mp_benchmark.hpp"
#include <gempba/gempba.hpp>

#include <iostream>
#include <chrono>
#include <numeric>


using namespace std::chrono;

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

int run(const benchmark_params &p_params) {

    auto *v_scheduler = gempba::mp::create_scheduler(gempba::mp::scheduler_topology::SEMI_CENTRALIZED);
    v_scheduler->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int v_rank = v_scheduler->rank_me();

    // logging in center only

    if (v_rank == 0) {
        std::cout << "USING OPTIMIZED ENCODING" << std::endl;
        std::cout << "USING SEMI-CENTRALIZED STRATEGY" << std::endl << std::endl;
        std::cout << "Running with parameters: " << std::endl;
        std::cout << "Job name : " << p_params.job_name << std::endl;
        std::cout << "Job id : " << p_params.job_id << std::endl;
        std::cout << "Nodes : " << p_params.nodes << std::endl;
        std::cout << "NTasks per node : " << p_params.ntasks_per_node << std::endl;
        std::cout << "NTasks per socket : " << p_params.ntasks_per_socket << std::endl;
        std::cout << "Threads per task : " << p_params.cpus_per_task << std::endl;
        std::cout << "Filename directory : " << p_params.dir_name << std::endl;
    }

    gempba::load_balancer *v_load_balancer = initiate_load_balancer(v_scheduler, gempba::balancing_policy::QUASI_HORIZONTAL);
    gempba::node_manager &v_node_manager = initiate_node_manager(v_scheduler, v_load_balancer);

    v_node_manager.set_goal(gempba::MINIMISE, gempba::score_type::I32);
    v_scheduler->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    v_node_manager.set_score(gempba::score::make(42)); // arbitrary initial score

    mp_benchmark v_instance(v_node_manager, v_load_balancer);

    auto v_function = std::bind(&mp_benchmark::explore, &v_instance, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5);

    v_scheduler->barrier();

    int v_pid = getpid(); // for debugging purposes
    spdlog::debug("rank {} is process ID : {}\n", v_rank, v_pid); // for debugging purposes

    v_scheduler->barrier();

    constexpr int v_runnable_id = 0;
    if (v_rank == 0) {
        std::cout << "Running realistic brute-force binary traversal:\n"
                << "  Depth = " << p_params.max_depth
                << " (leaves = " << (1ULL << p_params.max_depth) << ")\n";

        std::vector<double> v_initial_arg(ARG_SIZE);
        std::iota(v_initial_arg.begin(), v_initial_arg.end(), 0.0);
        const gempba::task_packet v_buffer = serializer(0, p_params.max_depth, v_initial_arg);

        // center process
        gempba::scheduler::center &v_center_view = v_scheduler->center_view();
        const gempba::task_packet &v_seed(v_buffer);
        v_center_view.run(v_seed, v_runnable_id);
    } else {
        // worker process
        gempba::scheduler::worker &v_worker_view = v_scheduler->worker_view();
        v_node_manager.set_thread_pool_size(p_params.cpus_per_task);

        std::function<std::tuple<int, int, std::vector<double> >(const gempba::task_packet &&)> v_deser = make_deserializer();
        auto v_runnable = gempba::mp::runnables::return_none::create(v_runnable_id, v_function, v_deser);

        std::map<int, std::shared_ptr<gempba::serial_runnable> > v_runnables;
        v_runnables[v_runnable->get_id()] = v_runnable;

        v_worker_view.run(v_node_manager, v_runnables);
    }
    v_scheduler->barrier();
    spdlog::info("rank {}, finished tasks successfully!!", v_rank);

    auto v_world_size = v_scheduler->world_size();

    // Synchronize stats across all processes
    v_scheduler->synchronize_stats();

    if (v_rank == 0) {
        int v_solution_size = 42;

        // Collect and print stats to console ***********
        std::vector<std::unique_ptr<gempba::stats> > v_stats_vector = v_scheduler->get_stats_vector();
        std::vector<std::size_t> v_received_tasks(v_world_size);
        std::vector<std::size_t> v_sent_tasks(v_world_size);
        std::vector<std::size_t> v_total_requests(v_world_size);
        std::vector<std::size_t> v_total_thread_requests(v_world_size);
        std::vector<double> v_idle_times(v_world_size);
        std::vector<double> v_elapsed_times(v_world_size);

        for (int v_rank = 0; std::cmp_less(v_rank, v_stats_vector.size()); ++v_rank) {
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
        print_to_summary_file(p_params,
                              v_world_size,
                              v_elapsed_times[0],
                              v_global_cpu_idle_time,
                              v_global_thread_request,
                              v_total_requests_at_center,
                              v_received_tasks,
                              v_sent_tasks,
                              v_total_thread_requests
                );
        // **************************************************************************
    }
    v_scheduler->barrier();
    return gempba::shutdown();
}

int main(const int argc, char *argv[]) {
    const benchmark_params v_params = parse(argc, argv);
    return run(v_params);
}
