#include "include/mp_bitvec_opt_enc.hpp"
#include "../GemPBA/MPI_Modules/MPI_Scheduler_Centralized.hpp"
#include "include/main.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <sstream>
#include <string>
#include <spdlog/spdlog.h>
#include <vector>
#include <unistd.h>

using namespace std::placeholders;


std::string createDir(std::string root) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return root;
}

std::string createDir(std::string root, std::string folder) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return createDir(root + "/" + folder + "/");
}

template<typename... T>
std::string createDir(std::string root, std::string folder, T... dir) {
    if (!fs::is_directory(root) || !fs::exists(root)) {
        fs::create_directory(root);
    }
    return createDir(root + "/" + folder, dir...);
}

void printToSummaryFile(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int cpus_per_task,
                        const std::string &filename_directory, gempba::SchedulerParent &mpiScheduler, int gsize,
                        int world_size, const std::vector<size_t> &threadRequests, const std::vector<int> &nTasksRecvd,
                        const std::vector<int> &nTasksSent, int solSize, double global_cpu_idle_time,
                        size_t totalThreadRequests) {
    std::string file_name = filename_directory.substr(filename_directory.find_last_of("/\\") + 1);
    const std::string targetDir = createDir("results", std::to_string(gsize), std::to_string(nodes));

    ofstream myfile;
    myfile.open(targetDir + file_name);
    myfile << "job id:\t" << job_id << std::endl;
    myfile << "nodes:\t" << nodes << std::endl;
    myfile << "ntasks-per-node:\t" << ntasks_per_node << std::endl;
    myfile << "ntasks-per-socket:\t" << ntasks_per_socket << std::endl;
    myfile << "cpus-per-task:\t" << cpus_per_task << std::endl;
    myfile << "graph size:\t\t" << gsize << std::endl;
    myfile << "cover size:\t\t" << solSize << std::endl;

    myfile << "process requests:\t\t" << mpiScheduler.getTotalRequests() <<   std::endl;

    myfile << "thread requests:\t\t" << totalThreadRequests << std::endl;
    myfile << "elapsed time:\t\t" << mpiScheduler.elapsedTime() << std::endl;
    myfile << "cpu idle time (global):\t" << global_cpu_idle_time << std::endl;
    myfile << "wall idle time (global):\t" << global_cpu_idle_time / (world_size - 1) << std::endl;

    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "tasks sent by rank " << rank << ":\t" << nTasksSent[rank] << std::endl;
    }
    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "tasks received by rank " << rank << ":\t" << nTasksRecvd[rank] << std::endl;
    }
    myfile << std::endl;

    for (int rank = 1; rank < world_size; rank++) {
        myfile << "rank " << rank << ", thread requests:\t" << threadRequests[rank] << std::endl;
    }
    myfile.close();
}

int run(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int threads_per_task, int prob,
        std::string &filename_directory) {

    std::cout << "USING OPTIMIZED ENCODING" << std::endl;
    std::cout << "USING CENTRALIZED STRATEGY" << std::endl;


    auto &branchHandler = gempba::BranchHandler::getInstance(); // parallel library

    // NOTE: instantiated object depends on SCHEDULER_CENTRALIZED macro
    auto &mpiScheduler = gempba::MPI_SchedulerCentralized::getInstance();

    int rank = mpiScheduler.rank_me();
    branchHandler.passMPIScheduler(&mpiScheduler);

    std::cout << "NUMTHREADS= " << threads_per_task << std::endl;


    VC_void_MPI_bitvec cover;
    auto function = std::bind(&VC_void_MPI_bitvec::mvcbitset, &cover, _1, _2, _3, _4,
                              _5); // target algorithm [all arguments]


    // initialize MPI and member variable linkin

    /* this is run by all processes, because it is a bitvector implementation,
        all processes should know the original graph ******************************************************/

    Graph graph;
    graph.readEdges(filename_directory);

    cover.init(graph, threads_per_task, filename_directory, prob);
    cover.setGraph(graph);

    int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
    gbitset allzeros(gsize);
    gbitset allones = ~allzeros;

    branchHandler.setRefValue(gsize); // thus, all processes know the best value so far
    branchHandler.setLookupStrategy(gempba::MINIMISE);

    int zero = 0;
    int solsize = graph.size();
    std::cout << "solsize=" << solsize << std::endl;
    mpiScheduler.barrier();

    std::string buffer = serializer(zero, allones, zero);


    std::cout << "Starting MPI node " << branchHandler.rank_me() << std::endl;

    mpiScheduler.barrier();

    int pid = getpid();                                       // for debugging purposes
    spdlog::info("rank {} is process ID : {}\n", rank, pid); // for debugging purposes

    mpiScheduler.barrier();

    if (rank == 0) {
        // center process
        mpiScheduler.runCenter(buffer.data(), buffer.size());
    } else {
        /*	worker process
            main thread will take care of Inter-process communication (IPC), dedicated core
            numThreads could be the number of physical cores managed by this process - 1
        */
        branchHandler.initThreadPool(threads_per_task - 1);

        std::function<std::shared_ptr<gempba::ResultHolderParent>(char*, int)> bufferDecoder = branchHandler.constructBufferDecoder<void, int, gbitset, int>(function, deserializer);
        std::function<std::pair<int, std::string>()> resultFetcher = branchHandler.constructResultFetcher();
        mpiScheduler.runNode(branchHandler, bufferDecoder, resultFetcher);
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

    if (rank != 0) { // rank 0 does not run the main function
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

    if (rank == 0) {
        auto solutions = mpiScheduler.fetchResVec();

        mpiScheduler.printStats();

        // print sumation of refValGlobal
        int solsize;
        std::stringstream ss;
        std::string buffer = mpiScheduler.fetchSolution(); // returns a std::stringstream

        ss << buffer;

        deserializer(ss, solsize);
        spdlog::info("Cover size : {} \n", solsize);

        double global_cpu_idle_time = 0;
        for (int i = 1; i < world_size; i++) {
            global_cpu_idle_time += idleTime[i];
        }
        spdlog::info("\nGlobal cpu idle time: {0:.6f} seconds\n\n\n", global_cpu_idle_time);

        // **************************************************************************

        for (int rank = 1; rank < world_size; rank++) {
            spdlog::info("tasks sent by rank {} = {} \n", rank, nTasksSent[rank]);
        }
        spdlog::info("\n");

        for (int rank = 1; rank < world_size; rank++) {
            spdlog::info("tasks received by rank {} = {} \n", rank, nTasksRecvd[rank]);
        }
        spdlog::info("\n");
        size_t totalThreadRequests = 0;
        for (int rank = 1; rank < world_size; rank++) {
            size_t rank_thread_requests = threadRequests[rank];
            totalThreadRequests += rank_thread_requests;

            spdlog::info("rank {}, thread requests: {} \n", rank, rank_thread_requests);
        }

        spdlog::info("\n\n\n");

        // print stats to a file ***********
        printToSummaryFile(job_id, nodes, ntasks_per_node, ntasks_per_socket, threads_per_task, filename_directory,
                           mpiScheduler, gsize,
                           world_size, threadRequests, nTasksRecvd, nTasksSent, solsize, global_cpu_idle_time,
                           totalThreadRequests);
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