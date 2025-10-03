#include <iostream>
#include <istream>
#include <string>
#include <unistd.h>
#include <spdlog/spdlog.h>

#include <gempba/gempba.hpp>
#include "include/main.hpp"
#include "include/mt_bitvect_opt_enc.hpp"


int run(int numThreads, int prob, std::string &filename) {
    using HolderType = gempba::result_holder<void, int, gbitset, int>;

    auto &branchHandler = gempba::mt::create_branch_handler(nullptr); // parallel library
    auto &dlb = gempba::dynamic_load_balancer_handler::getInstance();

    cout << "NUMTHREADS= " << numThreads << endl;

    VC_void_bitvec cover;
    auto function = std::bind(&VC_void_bitvec::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
    // initialize MPI and member variable linkin
    Graph graph;
    graph.readEdges(filename);

    //int initSize = graph.preprocessing();

    cover.init(graph, numThreads, filename, prob);
    cover.setGraph(graph);

    int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
    gbitset allzeros(gsize);
    gbitset allones = ~allzeros;

    branchHandler.set_score(gempba::score::make(gsize));
    branchHandler.set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int zero = 0;
    int solsize = graph.size();
    cout << "solsize=" << solsize << endl;

    //function(-1, 0, allones, 0, nullptr);
    //return 0;

    HolderType holder(dlb, -1);
    holder.holdArgs(zero, allones, zero);

    branchHandler.init_thread_pool(numThreads);
    const double start = branchHandler.get_wall_time();
    branchHandler.force_local_submit<void>(function, -1, holder);
    branchHandler.wait();
    const double end = branchHandler.get_wall_time();

    int v_solution = branchHandler.fetch_result<int>();
    double v_global_idle_time = branchHandler.get_pool_idle_time();
    double v_elapsed_time = end - start;
    size_t v_number_thread_requests = branchHandler.number_thread_requests();

    // **************************************************************************
    std::cout << "\n\n\n";
    spdlog::info("cover size: {}", v_solution);
    spdlog::info("global pool idle time: {0:.6f} seconds", v_global_idle_time);
    spdlog::info("elapsed time: {}", v_elapsed_time);
    spdlog::info("thread requests: {}", v_number_thread_requests);
    std::cout << "\n\n\n";
    // **************************************************************************

    return EXIT_SUCCESS;
}


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int thread_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(thread_per_task, prob, filename);
}
