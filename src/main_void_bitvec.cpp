#ifdef BITVECTOR_VC_THREAD

#include "../include/main.h"
#include "../include/Graph.hpp"

#include "../include/VC_void_bitvec.hpp"

#include <Resultholder/ResultHolder.hpp>
#include <BranchHandler/BranchHandler.hpp>
#include <DLB/DLB_Handler.hpp>

#include <iostream>
#include <istream>
#include <string>
#include <spdlog/spdlog.h>

#include <unistd.h>

int main_void_bitvec(int numThreads, int prob, std::string& filename)
{
    using HolderType = gempba::ResultHolder<void, int, gbitset, int>;

    auto &branchHandler = gempba::BranchHandler::getInstance(); // parallel library
    auto &dlb = gempba::DLB_Handler::getInstance();

    cout << "NUMTHREADS= " << numThreads << endl;

    VC_void_bitvec cover;
    auto function = std::bind(&VC_void_bitvec ::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
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
    branchHandler.setRefValStrategyLookup("minimise");

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
    spdlog::info("\n\n\nCover size : {} \n", solution);

    spdlog::info("Global pool idle time: {0:.6f} seconds\n\n\n", idl_tm);
    spdlog::info("Elapsed time: {}\n", end - start);

    // **************************************************************************

    spdlog::info("thread requests: {} \n", rqst);

    spdlog::info("\n\n\n");

    // **************************************************************************

    return 0;
}

#endif