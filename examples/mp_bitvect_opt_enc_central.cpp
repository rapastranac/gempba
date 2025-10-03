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
#include "include/mp_bitvec_opt_enc.hpp"

using namespace std::placeholders;

int run(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int threads_per_task, int prob,
        std::string &filename_directory) {

    std::cout << "USING OPTIMIZED ENCODING" << std::endl;
    std::cout << "USING CENTRALIZED STRATEGY" << std::endl;

    // NOTE: instantiated object depends on SCHEDULER_CENTRALIZED macro
    auto &mpiScheduler = *gempba::mp::create_scheduler(gempba::mp::scheduler_topology::CENTRALIZED);
    mpiScheduler.set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int rank = mpiScheduler.rank_me();

    std::cout << "NUMTHREADS= " << threads_per_task << std::endl;

    if (rank == 0) {
        // only because VertexCover is instantiated also in the center, but branch_handler is not used in the center
        gempba::mp::create_branch_handler(nullptr, nullptr);
    } else {
        gempba::mp::create_branch_handler(nullptr, &mpiScheduler.worker_view());
    }
    auto &branchHandler = gempba::get_branch_handler();; // parallel library

    VC_void_MPI_bitvec cover;
    auto function = std::bind(&VC_void_MPI_bitvec::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]


    // initialize MPI and member variable linkin

    /* this is run by all processes, because it is a bitvector implementation,
        all processes should know the original graph ******************************************************/

    Graph graph;
    graph.readEdges(filename_directory);

    cover.init(graph, threads_per_task, filename_directory, prob);
    cover.setGraph(graph);

    int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
    G_BITSET allzeros(gsize);
    G_BITSET allones = ~allzeros;

    branchHandler.set_score(gempba::score::make(gsize)); // thus, all processes know the best value so far
    branchHandler.set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int zero = 0;
    int solsize = graph.size();
    std::cout << "solsize=" << solsize << std::endl;
    mpiScheduler.barrier();

    gempba::task_packet v_buffer = serializer(zero, allones, zero);


    std::cout << "Starting MPI node " << mpiScheduler.rank_me() << std::endl;

    mpiScheduler.barrier();

    int pid = getpid(); // for debugging purposes
    spdlog::debug("rank {} is process ID : {}\n", rank, pid); // for debugging purposes

    mpiScheduler.barrier();

    if (rank == 0) {
        gempba::scheduler::center &v_center_view = mpiScheduler.center_view();
        // center process
        gempba::task_packet v_seed(v_buffer);
        v_center_view.run(v_seed);
    } else {
        gempba::scheduler::worker &v_worker_view = mpiScheduler.worker_view();
        /*	worker process
            main thread will take care of Inter-process communication (IPC), dedicated core
            numThreads could be the number of physical cores managed by this process - 1
        */
        branchHandler.init_thread_pool(threads_per_task);

        std::function<std::shared_ptr<gempba::result_holder_parent>(gempba::task_packet)> bufferDecoder = branchHandler.construct_buffer_decoder<void, int, G_BITSET, int>(function, deserializer);
        v_worker_view.run(branchHandler, bufferDecoder);
    }
    mpiScheduler.barrier();
    // *****************************************************************************************

    auto v_world_size = mpiScheduler.world_size();

    // Synchronize stats across all processes
    mpiScheduler.synchronize_stats();

    if (rank == 0) {
        // Retrieve solution
        auto && v_center_view = mpiScheduler.center_view();
        gempba::task_packet v_packet = v_center_view.get_result();
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(v_packet.data()), static_cast<int>(v_packet.size()));

        int v_solution_size;
        deserializer(v_ss, v_solution_size);
        spdlog::debug("Cover size : {} \n", v_solution_size);
        spdlog::debug("\n\n\n");

        // Collect and print stats to console ***********
        std::vector<std::unique_ptr<gempba::stats> > v_stats_vector = mpiScheduler.get_stats_vector();
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
        print_to_summary_file(job_id, nodes, ntasks_per_node, ntasks_per_socket, threads_per_task, filename_directory,
                              v_elapsed_times[0], gsize, v_world_size, v_total_thread_requests, v_received_tasks,
                              v_sent_tasks, v_solution_size, v_global_cpu_idle_time, v_global_thread_request,
                              v_total_requests_at_center);
        // **************************************************************************
    }
    return 0;
}

int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int job_id = params.job_id;
    int nodes = params.nodes;
    int ntasks_per_node = params.ntasks_per_node;
    int ntasks_per_socket = params.ntasks_per_socket;
    int cpus_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, prob, filename);
}
