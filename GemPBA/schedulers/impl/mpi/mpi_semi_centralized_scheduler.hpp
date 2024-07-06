#pragma once
#ifndef GEMPBA_MPISCHEDULER_H
#define GEMPBA_MPISCHEDULER_H

#include <spdlog/spdlog.h>
#include "schedulers/api/scheduler.hpp"
#include "BranchHandler/BranchHandler.hpp"

#define CENTER 0

#define STATE_RUNNING 1
#define STATE_ASSIGNED 2
#define STATE_AVAILABLE 3

#define TERMINATION_TAG 6
#define REFVAL_UPDATE_TAG 9
#define NEXT_PROCESS_TAG 10

#define HAS_RESULT_TAG 13
#define NO_RESULT_TAG 14

#define TIMEOUT_TIME 3


/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class BranchHandler;

    class MPISemiCentralizedScheduler final : public Scheduler {

        // Scheduler-specific variables
        LookupStrategy _lookupStrategy;
        int _global_reference_value;

        std::mutex mtx;
        std::atomic<bool> transmitting;

        int nTasksRecvd = 0;
        int nTasksSent = 0;
        int nRunning = 0;
        int nAvailable = 0;
        std::vector<int> processState; // state of the nodes: running, assigned or available
        Tree processTree;


        // statistics
        size_t totalRequests = 0;
        double start_time = 0;
        double end_time = 0;

        // MPI specific variables
        MPI_Comm _global_reference_value_communicator; // attached to win__global_reference_value
        MPI_Comm _next_process_communicator;      // attached to win_nextProcess
        MPI_Comm _world_communicator;          // world communicator

        int world_rank;              // get the rank of the process
        int world_size;              // get the number of processes/nodes
        char processor_name[128]; // name of the node


        // Private constructor for singleton
        MPISemiCentralizedScheduler() {
            initialize();
        }

    public:
        // Singleton instance creation
        static MPISemiCentralizedScheduler &getInstance() {
            static MPISemiCentralizedScheduler instance;
            return instance;
        }

        ~MPISemiCentralizedScheduler() override {
            finalize();
        }

        MPISemiCentralizedScheduler(const MPISemiCentralizedScheduler &) = delete;

        void operator=(const MPISemiCentralizedScheduler &) = delete;

        void runCenter(const char *seed, int count) override {
            //TODO... implementation of runCenter
        }

        void runNode(BranchHandler &branchHandler) override {
            //TODO... implementation of runNode
        }

    private:

        static void initialize() {
            // MPI specific initialization
            spdlog::info("Hello from MPI Scheduler\n");
        }

        static void finalize() {
            // MPI specific finalization
            spdlog::info("Goodbye from MPI Scheduler\n");
        }

    };
}

#endif //GEMPBA_MPISCHEDULER_H
