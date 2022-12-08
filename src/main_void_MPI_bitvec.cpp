#ifdef BITVECTOR_VC

#include "../include/main.h"
#include "../include/Graph.hpp"

#ifdef SCHEDULER_CENTRALIZED
#include "../GemPBA/MPI_Modules/MPI_Scheduler_Centralized.hpp"
#else
#include "../GemPBA/MPI_Modules/MPI_Scheduler.hpp"
#endif



#ifdef USE_LARGE_ENCODING
#include "../include/VC_void_bitvec_enc.hpp"
#else
#include "../include/VC_void_MPI_bitvec.hpp"
#endif


#include "../GemPBA/Resultholder/ResultHolder.hpp"
#include "../GemPBA/BranchHandler/BranchHandler.hpp"
#include "../GemPBA/DLB/DLB_Handler.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

void printToSummaryFile(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int cpus_per_task,
                        const string &filename_directory, GemPBA::MPI_Scheduler &mpiScheduler, int gsize,
                        int world_size, const vector<size_t> &threadRequests, const vector<int> &nTasksRecvd,
                        const vector<int> &nTasksSent, int solSize, double global_cpu_idle_time,
                        size_t totalThreadRequests);

int main_void_MPI_bitvec(int job_id, int nodes, int ntasks_per_node, int ntasks_per_socket, int cpus_per_task, int prob,
                         std::string &filename_directory)
{
#ifdef USE_LARGE_ENCODING
	cout<<"USING LARGE ENCODING"<<endl;
#else
	cout<<"USING OPTIMIZED ENCODING"<<endl;
#endif
#ifdef SCHEDULER_CENTRALIZED
	cout<<"USING CENTRALIZED STRATEGY"<<endl;
#else
	cout<<"USING SEMI-CENTRALIZED STRATEGY"<<endl;
#endif


	auto &branchHandler = GemPBA::BranchHandler::getInstance(); // parallel library

	// NOTE: instantiated object depends on SCHEDULER_CENTRALIZED macro
	auto &mpiScheduler = GemPBA::MPI_Scheduler::getInstance();

	int rank = mpiScheduler.rank_me();
	branchHandler.passMPIScheduler(&mpiScheduler);

	cout << "NUMTHREADS= " << cpus_per_task << endl;

#ifdef USE_LARGE_ENCODING
	VC_void_MPI_bitvec_enc cover;
	auto function = std::bind(&VC_void_MPI_bitvec_enc::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
#else
	VC_void_MPI_bitvec cover;
	auto function = std::bind(&VC_void_MPI_bitvec::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
#endif

																							// initialize MPI and member variable linkin

	/* this is run by all processes, because it is a bitvector implementation,
		all processes should know the original graph ******************************************************/

	Graph graph;
	graph.readEdges(filename_directory);

	cover.init(graph, cpus_per_task, filename_directory, prob);
	cover.setGraph(graph);

	int gsize = graph.adj.size() + 1; //+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)
	gbitset allzeros(gsize);
	gbitset allones = ~allzeros;

	branchHandler.setRefValue(gsize); // thus, all processes know the best value so far
	branchHandler.setRefValStrategyLookup("minimise");

	int zero = 0;
	int solsize = graph.size();
	std::cout << "solsize=" << solsize << endl;
	mpiScheduler.barrier();
#ifdef USE_LARGE_ENCODING
	std::string buffer = serializer(zero, cover.init_graphbits, zero);
#else
	std::string buffer = serializer(zero, allones, zero);
#endif

	std::cout << "Starting MPI node " << branchHandler.rank_me() << std::endl;

	mpiScheduler.barrier();

	int pid = getpid();									   // for debugging purposes
	fmt::print("rank {} is process ID : {}\n", rank, pid); // for debugging purposes

	mpiScheduler.barrier();

	if (rank == 0)
	{
		// center process
		mpiScheduler.runCenter(buffer.data(), buffer.size());
	}
	else
	{
		/*	worker process
			main thread will take care of Inter-process communication (IPC), dedicated core
			numThreads could be the number of physical cores managed by this process - 1
		*/
        branchHandler.initThreadPool(cpus_per_task - 1);
#ifdef USE_LARGE_ENCODING
	auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, unordered_map<int, gbitset>, int>(function, deserializer);
#else
	auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, gbitset, int>(function, deserializer);
#endif
		
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
	{ // rank 0 does not run the main function
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

	if (rank == 0)
	{
		auto solutions = mpiScheduler.fetchResVec();

		mpiScheduler.printStats();

		// print sumation of refValGlobal
		int solsize;
		std::stringstream ss;
		std::string buffer = mpiScheduler.fetchSolution(); // returns a stringstream

		ss << buffer;

		deserializer(ss, solsize);
		fmt::print("Cover size : {} \n", solsize);

		double global_cpu_idle_time = 0;
		for (int i = 1; i < world_size; i++)
		{
			global_cpu_idle_time += idleTime[i];
		}
		fmt::print("\nGlobal cpu idle time: {0:.6f} seconds\n\n\n", global_cpu_idle_time);

		// **************************************************************************

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("tasks sent by rank {} = {} \n", rank, nTasksSent[rank]);
		}
		fmt::print("\n");

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("tasks received by rank {} = {} \n", rank, nTasksRecvd[rank]);
		}
		fmt::print("\n");
		size_t totalThreadRequests = 0;
		for (int rank = 1; rank < world_size; rank++)
		{
			size_t rank_thread_requests = threadRequests[rank];
			totalThreadRequests += rank_thread_requests;

			fmt::print("rank {}, thread requests: {} \n", rank, rank_thread_requests);
		}

		fmt::print("\n\n\n");

		// print stats to a file ***********
        printToSummaryFile(job_id, nodes, ntasks_per_node, ntasks_per_socket, cpus_per_task, filename_directory,
                           mpiScheduler, gsize,
                           world_size, threadRequests, nTasksRecvd, nTasksSent, solsize, global_cpu_idle_time,
                           totalThreadRequests);
        // **************************************************************************
	}
	return 0;
}

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
                        const string &filename_directory, GemPBA::MPI_Scheduler &mpiScheduler, int gsize,
                        int world_size, const vector<size_t> &threadRequests, const vector<int> &nTasksRecvd,
                        const vector<int> &nTasksSent, int solSize, double global_cpu_idle_time,
                        size_t totalThreadRequests) {
    string file_name = filename_directory.substr(filename_directory.find_last_of("/\\") + 1);
    const std::string targetDir = createDir("results", std::to_string(gsize), std::to_string(nodes));

    ofstream myfile;
    myfile.open(targetDir + file_name);
    myfile << "job id:\t" << job_id << endl;
    myfile << "nodes:\t" << nodes << endl;
    myfile << "ntasks-per-node:\t" << ntasks_per_node << endl;
    myfile << "ntasks-per-socket:\t" << ntasks_per_socket << endl;
    myfile << "cpus-per-task:\t" << cpus_per_task << endl;
    myfile << "graph size:\t\t" << gsize << endl;
    myfile << "cover size:\t\t" << solSize << endl;
#ifdef SCHEDULER_CENTRALIZED
    myfile << "process requests:\t\t" << mpiScheduler.getTotalRequests() << endl;
#endif
    myfile << "thread requests:\t\t" << totalThreadRequests << endl;
    myfile << "elapsed time:\t\t" << mpiScheduler.elapsedTime() << endl;
    myfile << "cpu idle time (global):\t" << global_cpu_idle_time << endl;
    myfile << "wall idle time (global):\t" << global_cpu_idle_time / (world_size - 1) << endl;

    myfile << endl;

    for (int rank = 1; rank < world_size; rank++)
    {
        myfile << "tasks sent by rank " << rank << ":\t" << nTasksSent[rank] << endl;
    }
    myfile << endl;

    for (int rank = 1; rank < world_size; rank++)
    {
        myfile << "tasks received by rank " << rank << ":\t" << nTasksRecvd[rank] << endl;
    }
    myfile << endl;

    for (int rank = 1; rank < world_size; rank++)
    {
        myfile << "rank " << rank << ", thread requests:\t" << threadRequests[rank] << endl;
    }
    myfile.close();
}

#endif
