#ifdef BITVECTOR_VC

#include "../include/main.h"
#include "../include/Graph.hpp"

#ifdef SCHEDULER_CENTRALIZED
#include "../GemPBA/MPI_Modules/MPI_Scheduler_Centralized.hpp"
#else
#include "../GemPBA/MPI_Modules/MPI_Scheduler.hpp"
#endif

#include "../include/VC_void_MPI_bitvec.hpp"

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

int main_void_MPI_bitvec(int job_id,
						 int nodes,
						 int ntasks_per_node,
						 int ntasks_per_socket,
						 int cpus_per_task,
						 int numThreads,
						 int prob,
						 std::string &filename_directory)
{

	auto &branchHandler = GemPBA::BranchHandler::getInstance(); // parallel library

	// NOTE: instantiated object depends on SCHEDULER_CENTRALIZED macro
	auto &mpiScheduler = GemPBA::MPI_Scheduler::getInstance();

	int rank = mpiScheduler.rank_me();
	branchHandler.passMPIScheduler(&mpiScheduler);

	cout << "NUMTHREADS= " << numThreads << endl;

	VC_void_MPI_bitvec cover;
	auto function = std::bind(&VC_void_MPI_bitvec ::mvcbitset, &cover, _1, _2, _3, _4, _5); // target algorithm [all arguments]
																							// initialize MPI and member variable linkin

	/* this is run by all processes, because it is a bitvector implementation,
		all processes should know the original graph ******************************************************/

	Graph graph;
	graph.readEdges(filename_directory);

	cover.init(graph, numThreads, filename_directory, prob);
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
	std::string buffer = serializer(zero, allones, zero);

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
		branchHandler.initThreadPool(numThreads);
		auto bufferDecoder = branchHandler.constructBufferDecoder<void, int, gbitset, int>(function, deserializer);
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

		for (int rank = 1; rank < world_size; rank++)
		{
			fmt::print("rank {}, thread requests: {} \n", rank, threadRequests[rank]);
		}

		fmt::print("\n\n\n");

		// **************************************************************************
	}
	return 0;
}

#endif
