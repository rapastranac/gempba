#include "include/main.hpp"
#include "include/mt_bitvect_opt_enc.hpp"

#include <Resultholder/ResultHolder.hpp>
#include <BranchHandler/branch_handler.hpp>
#include <DLB/DLB_Handler.hpp>

#include <iostream>
#include <istream>
#include <string>
#include <spdlog/spdlog.h>

#include <unistd.h>

int run(int numThreads, int prob, std::string &filename) {
    using HolderType = gempba::ResultHolder<void, int, gbitset, int>;

    auto &branchHandler = gempba::branch_handler::getInstance(); // parallel library
    auto &dlb = gempba::DLB_Handler::getInstance();

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

    branchHandler.setRefValue(gsize);
    branchHandler.set_goal(gempba::MINIMISE);

    int zero = 0;
    int solsize = graph.size();
    cout << "solsize=" << solsize << endl;

    //function(-1, 0, allones, 0, nullptr);
    //return 0;

    HolderType holder(dlb, -1);
    holder.holdArgs(zero, allones, zero);

    double start = branchHandler.WTime();
    branchHandler.initThreadPool(numThreads);
    branchHandler.force_push<void>(function, -1, holder);
    branchHandler.wait();
    double end = branchHandler.WTime();

    double idl_tm = branchHandler.getPoolIdleTime();
    size_t rqst = branchHandler.number_thread_requests();

    int solution = branchHandler.fetchSolution<int>();
    spdlog::debug("\n\n\nCover size : {} \n", solution);

    spdlog::debug("Global pool idle time: {0:.6f} seconds\n\n\n", idl_tm);
    spdlog::debug("Elapsed time: {}\n", end - start);

    // **************************************************************************

    spdlog::debug("thread requests: {} \n", rqst);

    spdlog::debug("\n\n\n");

    // **************************************************************************

    return 0;
}


int main(int argc, char *argv[]) {
    Params params = parse(argc, argv);

    int thread_per_task = params.cpus_per_task;
    int prob = params.prob;
    auto filename = params.filename;

    return run(thread_per_task, prob, filename);
}
