#ifdef VC_VOID_MPI

#include "../include/main.h"
#include "../include/Graph.hpp"
#include "../MPI_Modules/MPI_Scheduler.hpp"

#include "../include/VC_void_MPI.hpp"

#include "../include/Resultholder/ResultHolder.hpp"
#include "../include/BranchHandler.hpp"
#include "../include/DLB_Handler.hpp"

#include "utils/Tree.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>
#include <spdlog/spdlog.h>
#include <vector>
#include <unistd.h>

auto &dlb = gempba::DLB_Handler::getInstance();
auto &branchHandler = gempba::BranchHandler::getInstance(); // parallel library

std::mutex mtx;
size_t leaves = 0;

int k = 20;
size_t multiple = 1024; //static_cast<size_t>(pow(2, 18));

void foo(int id, int depth, float treeIdx, void *parent)
{
    using HolderType = gempba::ResultHolder<void, int, float>;
    //spdlog::info("rank {}, id : {} depth : {} treeIdx : {}\n", branchHandler.rank_me(), id, depth, treeIdx); // node id in the tree

    if (depth >= k)
    {
        std::scoped_lock<std::mutex> lck(mtx);
        leaves++;
        branchHandler.holdSolution(leaves, leaves, serializer);

        int tmp = branchHandler.refValue();
        if ((int)leaves > tmp)
            if (leaves % multiple == 0)
                branchHandler.updateRefValue(leaves);

        //spdlog::info("rank {}, Leaves : {}\n", branchHandler.rank_me(), leaves);
        return;
    }
    int newDepth = depth + 1;
    HolderType hol_l(dlb, id, parent);
    HolderType hol_r(dlb, id, parent);
    hol_l.setDepth(depth);
    hol_r.setDepth(depth);

    dlb.linkVirtualRoot(id, parent, hol_l, hol_r);

    hol_l.bind_branch_checkIn([&]()
                              {
                                  float newTreeIdx = treeIdx + pow(2, depth);
                                  hol_l.holdArgs(newDepth, newTreeIdx);
                                  return true;
                              });

    hol_r.bind_branch_checkIn([&]()
                              {
                                  hol_r.holdArgs(newDepth, treeIdx);
                                  return true;
                              });

    //if (depth < 6)
    if (hol_l.evaluate_branch_checkIn())
        branchHandler.try_push_MP<void>(foo, id, hol_l, serializer);
    //else
    //	//	foo(id, newDepth, newTreeIdx, nullptr);
    //	branchHandler.try_push_MT<void>(foo, id, hol_l); // only threads

    //std::this_thread::sleep_for(1s);

    //foo(id, newDepth, treeIdx, nullptr);

    if (hol_r.evaluate_branch_checkIn())
        branchHandler.forward<void>(foo, id, hol_r);
}

int main_void_MPI(int numThreads, int prob, std::string &filename)
{
    //using HolderType = gempba::ResultHolder<void, int, Graph>;

    Graph graph;
    Graph oGraph;
    VC_void_MPI cover;

    auto mainAlgo = std::bind(&VC_void_MPI::mvc, &cover, _1, _2, _3, _4); // target algorithm [all arguments]

    auto &mpiScheduler = gempba::MPI_Scheduler::getInstance(); // MPI MPI_Scheduler
    int rank = mpiScheduler.rank_me();
    //HolderType holder(handler);									//it creates a ResultHolder, required to retrive result
    branchHandler.passMPIScheduler(&mpiScheduler);
    gempba::ResultHolder<void, int, float> hldr(dlb, -1, nullptr);
    //float val = 845.515;

    //	int frst = 0;
    //	float scnd = 1;
    //	hldr.holdArgs(frst, scnd);
    //	branchHandler.initThreadPool(numThreads);
    //
    //	//foo(-1, 0, -1, nullptr);
    //	//auto res = bar(-1, 0, -1, nullptr);
    //
    //	//while (!branchHandler.isDone())
    //	//	spdlog::info("Not done yet !!\n");
    //
    //	branchHandler.try_push_MT<void>(foo, -1, hldr);
    //	std::this_thread::sleep_for(1s); //emulates quick task
    //	branchHandler.wait();
    //	spdlog::info("Leaves : {}\n", leaves);
    //	spdlog::info("Thread calls : {}\n", branchHandler.number_thread_requests());
    //
    //	return 0;

    /* previous input and output required before following condition
    thus, other nodes know the data type*/
    //using HolderType = gempba::ResultHolder<void, int, float>;
    using HolderType = gempba::ResultHolder<void, int, Graph>;

    HolderType holder(dlb, -1); //it creates a ResultHolder, required to retrive result
    int depth = 0;

    graph.readEdges(filename);

    std::vector<Graph> graphs;

    for (int i = 1; i < 20; i++)
    {
        graphs.push_back(graph);
    }

    // ******************************************************************
    // temp for mini-cluster
    //if (rank == 1)
    //	numThreads = 5; //cuz center is also in this machine
    //if (rank == 2)
    //	numThreads = 4;
    // ******************************************************************

    //int preSize = graph.preprocessing();
    size_t k_mm = cover.maximum_matching(graph);
    size_t k_uBound = graph.max_k();
    size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
    branchHandler.setRefValue(k_prime);
    cover.init(graph, numThreads, filename, prob);

    //	holder.holdArgs(depth, graph);
    //	branchHandler.setRefValStrategyLookup("minimise");
    //	branchHandler.initThreadPool(numThreads);
    //	branchHandler.try_push_MT<void>(mainAlgo, -1, holder);
    //	std::this_thread::sleep_for(1s); //emulates quick task
    //	branchHandler.wait();
    //	mpiScheduler.finalize();
    //	return 0;

    // foo(int id, int depth, float treeIdx, void *parent) ********************
    float treeIdx = 1;
    //std::string buffer = serializer(depth, treeIdx);
    std::string buffer = serializer(depth, graph);

    // ************************************************************************
    //branchHandler.setRefValStrategyLookup("maximise");
    branchHandler.setRefValStrategyLookup("minimise");

    int pid = getpid();
    spdlog::info("rank {} is process ID : {}\n", rank, pid);

    if (rank == 0)
        mpiScheduler.runCenter(buffer.data(), buffer.size());
    else
    {
        branchHandler.initThreadPool(numThreads);
        //auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, float>(foo, deserializer);
        auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, Graph>(mainAlgo, deserializer);
        auto resultFetcher = branchHandler.constructResultFetcher();
        mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher, serializer);
    }

    mpiScheduler.barrier();

    // *****************************************************************************************
    // this is a generic way of getting information from all the other processes after execution retuns
    auto world_size = mpiScheduler.getWorldSize();
    std::vector<double> idleTime(world_size);
    std::vector<size_t> threadRequests(world_size);
    std::vector<int> nTasksRecvd(world_size);
    std::vector<int> nTasksSent(world_size);

    double idl_tm = 0;
    size_t rqst = 0;
    int taskRecvd;
    int taskSent;

    if (rank != 0)
    { //rank 0 does not run an instance of BranchHandler
        idl_tm = branchHandler.getPoolIdleTime();
        rqst = branchHandler.number_thread_requests();

        taskRecvd = mpiScheduler.tasksRecvd();
        taskSent = mpiScheduler.tasksSent();
    }

    // here below, idl_tm is the idle time of the other ranks, which is gathered by .allgather() and stored in
    // a contiguos array
    mpiScheduler.allgather(idleTime.data(), &idl_tm, MPI_DOUBLE);
    mpiScheduler.allgather(threadRequests.data(), &rqst, MPI_UNSIGNED_LONG_LONG);

    mpiScheduler.gather(&taskRecvd, 1, MPI_INT, nTasksRecvd.data(), 1, MPI_INT, 0);
    mpiScheduler.gather(&taskSent, 1, MPI_INT, nTasksSent.data(), 1, MPI_INT, 0);

    // *****************************************************************************************
    //ipc_handler.finalize();
    //return 0;

    if (rank == 0)
    {
        auto solutions = mpiScheduler.fetchResVec();

        int summation = 0;
        for (size_t i = 0; i < mpiScheduler.getWorldSize(); i++)
        {
            if (solutions[i].first != -1)
                summation += solutions[i].first;
        }
        spdlog::info("\n\n");
        spdlog::info("Summation of refValGlobal : {}\n", summation);

        //std::this_thread::sleep_for(std::chrono::milliseconds(2000)); // to let other processes to print
        mpiScheduler.printStats();

        //print sumation of refValGlobal

        std::stringstream ss;
        std::string buffer = mpiScheduler.fetchSolution(); // returns a stringstream

        ss << buffer;

        deserializer(ss, oGraph);
        auto cv = oGraph.postProcessing();
        spdlog::info("Cover size : {} \n", cv.size());

        double sum = 0;
        for (int i = 1; i < world_size; i++)
        {
            sum += idleTime[i];
        }
        spdlog::info("\nGlobal pool idle time: {0:.6f} seconds\n\n\n", sum);

        // **************************************************************************

        for (int rank = 1; rank < world_size; rank++)
        {
            spdlog::info("tasks sent by rank {} = {} \n", rank, nTasksSent[rank]);
        }
        spdlog::info("\n");

        for (int rank = 1; rank < world_size; rank++)
        {
            spdlog::info("tasks received by rank {} = {} \n", rank, nTasksRecvd[rank]);
        }
        spdlog::info("\n");

        for (int rank = 1; rank < world_size; rank++)
        {
            spdlog::info("rank {}, thread requests: {} \n", rank, threadRequests[rank]);
        }

        spdlog::info("\n\n\n");

        // **************************************************************************
    }
    return 0;
}

#endif