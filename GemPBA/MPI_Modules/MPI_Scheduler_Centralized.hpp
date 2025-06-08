#pragma once
#ifndef MPI_SCHEDULER_CENTRALIZED_HPP
#define MPI_SCHEDULER_CENTRALIZED_HPP

#include "centralized_utils.hpp"
#include "scheduler_parent.hpp"
#include "Resultholder/ResultHolderParent.hpp"
#include "utils/Queue.hpp"
#include "utils/tree.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdio>
#include <cstdlib> /* srand, rand */
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mpi.h>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <spdlog/spdlog.h>

// max memory is in mb, e.g. 1024 * 10 = 10 GB
#define MAX_MEMORY_MB (1024 * 10)

#define CENTER 0

#define STATE_RUNNING 1
#define STATE_ASSIGNED 2
#define STATE_AVAILABLE 3

#define TERMINATION_TAG 6
#define REFVAL_UPDATE_TAG 9

#define TASK_FROM_CENTER_TAG 12

#define HAS_RESULT_TAG 13
#define NO_RESULT_TAG 14

#define TASK_FOR_CENTER 15

#define CENTER_IS_FULL_TAG 16
#define CENTER_IS_FREE_TAG 17

#define TIMEOUT_TIME 2


#define CENTER_NBSTORED_TASKS_PER_PROCESS 1000

namespace gempba {

    class BranchHandler;

    // inter process communication handler
    class MPI_SchedulerCentralized final : public SchedulerParent {

        std::priority_queue<std::pair<char*, int>, std::vector<std::pair<char*, int>>, TaskComparator> center_queue; //message, size
        //std::vector<std::pair<char *, int>> center_queue;

        int max_queue_size;
        bool center_last_full_status = false;

        double time_centerfull_sent = 0;


        std::vector<std::pair<char*, int>> local_outqueue;
        std::vector<std::pair<char*, int>> local_inqueue;

    public:
        ~MPI_SchedulerCentralized() override {
            finalize();
        }

        static MPI_SchedulerCentralized& getInstance() {
            static MPI_SchedulerCentralized instance;
            return instance;
        }

        int rank_me() const override {
            return world_rank;
        }

        std::string fetchSolution() override {
            for (int rank = 1; rank < world_size; rank++) {
                if (bestResults[rank].first == refValueGlobal) {
                    return bestResults[rank].second;
                }
            }
            return {}; // no solution found
        }

        std::vector<std::pair<int, std::string>> fetchResVec() override {
            return bestResults;
        }


        void printStats() override {
            spdlog::debug("\n \n \n");
            spdlog::debug("*****************************************************\n");
            spdlog::debug("Elapsed time : {:4.3f} \n", elapsedTime());
            spdlog::debug("Total number of requests : {} \n", totalRequests);
            spdlog::debug("*****************************************************\n");
            spdlog::debug("\n \n \n");
        }

        size_t getTotalRequests() const override {
            return totalRequests;
        }

        void set_custom_initial_topology(tree&& p_tree) override {
            processTree = std::move(p_tree);
            m_custom_initial_topology = true;
        }

        double elapsedTime() const override {
            return (end_time - start_time) - static_cast<double>(TIMEOUT_TIME);
        }

        int nextProcess() const override {
            return 0;
        }

        void allgather(void* recvbuf, void* sendbuf, MPI_Datatype mpi_datatype) override {
            MPI_Allgather(sendbuf, 1, mpi_datatype, recvbuf, 1, mpi_datatype, world_Comm);
            MPI_Barrier(world_Comm);
        }

        void gather(void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype,
                    int root) override {
            MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, world_Comm);
        }

        int getWorldSize() const override {
            return world_size;
        }

        int tasksRecvd() const override {
            return nTasksRecvd;
        }

        int tasksSent() const override {
            return nTasksSent;
        }

        void barrier() override {
            if (world_Comm != MPI_COMM_NULL)
                MPI_Barrier(world_Comm);
        }

        bool openSendingChannel() override {
            if (mtx.try_lock()) // acquires mutex
            {
                if (!transmitting.load()) // check if transmission in progress
                {
                    if (!isCenterFull) // check if center is actually waiting for a task
                    {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                mtx.unlock();
            }
            return false;
        }

        /* this should be invoked only if channel is open*/
        void closeSendingChannel() override {
            mtx.unlock();
        }

        void setRefValStrategyLookup(bool maximisation) override {
            this->maximisation = maximisation;

            if (!maximisation) // minimisation
                refValueGlobal = INT_MAX;
        }


        void runNode(BranchHandler& branchHandler, std::function<std::shared_ptr<ResultHolderParent>(char*, int)>& bufferDecoder,
                     std::function<std::pair<int, std::string>()>& resultFetcher) override {
            MPI_Barrier(world_Comm);

            while (true) {
                MPI_Status status;
                int count; // count to be received
                int flag = 0;

                while (!flag) // this allows  to receive refValue or nextProcess even if this process has turned into waiting mode
                {
                    if (probe_refValue()) // different communicator
                        continue; // center might update this value even if this process is idle

                    if (probe_centerRequest()) // different communicator
                        continue; // center might update this value even if this process is idle

                    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &flag, &status); // for regular messages
                    if (flag)
                        break;
                }
                MPI_Get_count(&status, MPI_CHAR, &count); // receives total number of datatype elements of the message

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, received message from rank {}, tag {}, count : {}\n", world_rank, status.MPI_SOURCE, status.MPI_TAG, count);
                #endif
                char* message = new char[count];
                MPI_Recv(message, count, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

                if (isTerminated(status.MPI_TAG)) {
                    delete[] message;
                    break;
                }

                if (status.MPI_TAG == TASK_FROM_CENTER_TAG) {

                    notifyRunningState();
                    nTasksRecvd++;

                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("rank {}, pushing buffer to thread pool", world_rank, status.MPI_SOURCE);
                    #endif
                    //  push to the thread pool *********************************************************************
                    std::shared_ptr<ResultHolderParent> holder = bufferDecoder(message, count); // holder might be useful for non-void functions
                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("... DONE\n", world_rank, status.MPI_SOURCE);
                    #endif
                    // **********************************************************************************************

                    taskFunneling(branchHandler);
                    notifyAvailableState();

                    delete[] message;

                    // TODO: refVal
                }
            }
            /**
             * TODO.. send results to the rank the task was sent from
             * this applies only when parallelising non-void functions
             */

            sendSolution(resultFetcher);
        }

        /* enqueue a message which will be sent to the center
         */
        void push(std::string&& message) override {
            if (message.size() == 0) {
                auto str = fmt::format("rank {}, attempted to send empty buffer \n", world_rank);
                throw std::runtime_error(str);
            }

            transmitting = true;

            auto pck = std::make_shared<std::string>(std::forward<std::string&&>(message));
            auto _message = new std::string(*pck);

            if (!q.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            q.push(_message);

            closeSendingChannel();
        }

    private:
        // when a node is working, it loops through here
        void taskFunneling(BranchHandler& branchHandler);

        // checks for a ref value update from center
        int probe_refValue() {
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(CENTER, REFVAL_UPDATE_TAG, refValueGlobal_Comm, &flag, &status);

            if (flag) {
                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, about to receive refValue from Center\n", world_rank);
                #endif

                MPI_Recv(&refValueGlobal, 1, MPI_INT, CENTER, REFVAL_UPDATE_TAG, refValueGlobal_Comm, &status);

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("rank {}, received refValue: {} from Center\n", world_rank, refValueGlobal);
                #endif
            }

            return flag;
        }

        // checks for a new assigned process from center
        bool probe_centerRequest() {
            int flag1 = 0;
            int flag2 = 0;
            MPI_Status status;
            MPI_Iprobe(CENTER, CENTER_IS_FULL_TAG, centerFullness_Comm, &flag1, &status);

            if (flag1) {
                char buf;
                MPI_Recv(&buf, 1, MPI_CHAR, CENTER, CENTER_IS_FULL_TAG, centerFullness_Comm, &status);
                isCenterFull = true;
                #if GEMPBA_DEBUG_COMMENTS
                std::cout << "Node " << rank_me() << " received full center" << std::endl;
                #endif
            }

            MPI_Iprobe(CENTER, CENTER_IS_FREE_TAG, centerFullness_Comm, &flag2, &status);

            if (flag2) {
                char buf;
                MPI_Recv(&buf, 1, MPI_CHAR, CENTER, CENTER_IS_FREE_TAG, centerFullness_Comm, &status);
                isCenterFull = false;
                #if GEMPBA_DEBUG_COMMENTS
                std::cout << "Node " << rank_me() << " received free center" << std::endl;
                #endif
            }

            return (flag1 || flag2);
        }

        // if ref value received, it attempts updating local value
        // if local value is better than the one in center, then local best value is sent to center
        void updateRefValue(BranchHandler& branchHandler);

        bool isTerminated(int TAG) {
            if (TAG == TERMINATION_TAG) {
                spdlog::debug("rank {} exited\n", world_rank);
                MPI_Barrier(world_Comm);
                return true;
            }
            return false;
        }

        void notifyAvailableState() {
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {} entered notifyAvailableState()\n", world_rank);
            #endif

            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, CENTER, STATE_AVAILABLE, world_Comm);
        }

        void notifyRunningState() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, CENTER, STATE_RUNNING, world_Comm);
        }

        void sendTaskToCenter(std::string& message) {
            MPI_Send(message.data(), message.size(), MPI_CHAR, CENTER, TASK_FOR_CENTER, world_Comm);
        }

    public:
    private:
        /*	send solution attained from node to the center node */
        void sendSolution(auto&& resultFetcher) {
            auto [refVal, buffer] = resultFetcher();
            if (buffer.starts_with("Empty")) {
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, NO_RESULT_TAG, world_Comm);
            } else {
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, 0, HAS_RESULT_TAG, world_Comm);
                MPI_Send(&refVal, 1, MPI_INT, 0, HAS_RESULT_TAG, world_Comm);
            }
        }

        /* it returns the substraction between end and start*/
        double difftime(double start, double end) {
            return end - start;
        }

    public:
        void clearBuffer() {
            if (center_queue.empty())
                return;

            for (int rank = 1; rank < world_size; rank++) {
                if (processState[rank] == STATE_AVAILABLE) {
                    //pair<char *, size_t> msg = center_queue.back();
                    //center_queue.pop_back();
                    std::pair<char*, size_t> msg = center_queue.top();
                    center_queue.pop();

                    MPI_Send(msg.first, msg.second, MPI_CHAR, rank, TASK_FROM_CENTER_TAG, world_Comm);
                    delete[] msg.first;
                    processState[rank] = STATE_ASSIGNED;

                    if (center_queue.empty())
                        return;
                }
            }
        }

        void handleFullMessaging() {
            size_t currentMemory = getCurrentRSS() / (1024 * 1024); // ram usage in megabytes


            if (!center_last_full_status) {
                // last iter, center wasn't full but now it is => warn nodes to stop sending
                if (currentMemory > MAX_MEMORY_MB ||
                    center_queue.size() > CENTER_NBSTORED_TASKS_PER_PROCESS * world_size) {
                    for (int rank = 1; rank < world_size; rank++) {
                        char tmp = 0;
                        MPI_Send(&tmp, 1, MPI_CHAR, rank, CENTER_IS_FULL_TAG, centerFullness_Comm);
                    }
                    center_last_full_status = true;
                    time_centerfull_sent = MPI_Wtime();

                    //cout << "CENTER IS FULL" << endl;
                }
            } else {
                // last iter, center was full but now it has space => warn others it's ok
                if (currentMemory <= 0.9 * MAX_MEMORY_MB &&
                    center_queue.size() < CENTER_NBSTORED_TASKS_PER_PROCESS * world_size * 0.8) {
                    for (int rank = 1; rank < world_size; rank++) {
                        char tmp = 0;
                        MPI_Send(&tmp, 1, MPI_CHAR, rank, CENTER_IS_FREE_TAG, centerFullness_Comm);
                    }
                    center_last_full_status = false;

                    //cout << "CENTER IS NOT FULL ANYMORE" << endl;
                }
            }
        }

        /*	run the center node */
        void runCenter(const char* p_seed, const int p_seed_size) override {
            std::cout << "Starting centralized scheduler" << std::endl;
            MPI_Barrier(world_Comm);
            start_time = MPI_Wtime();

            sendSeed(p_seed, p_seed_size);

            int nbloops = 0;
            while (true) {
                nbloops++;
                int buffer;
                char* buffer_char = nullptr;
                int buffer_char_count = 0;
                MPI_Status status;
                MPI_Request request;
                int ready;

                int flag;
                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &flag, &status);

                /*if (nbloops % 100 == 0)
                {
                    cout<<"CENTER nb loops = "<<nbloops<<" nrunning="<<nRunning<<endl;
                    cout<<"STATES=";
                    for (int i = 1; i < world_size;++i)
                    {
                        cout<<processState[i]<<"  ";
                    }
                    cout<<endl;
                    cout<<"WARNED=";
                    for (int i = 1; i < world_size;++i)
                    {
                        cout<<processes_center_asked[i]<<"  ";
                    }
                    cout<<endl;
                }*/

                if (!flag) {
                    double begin = MPI_Wtime();

                    while (!flag && (difftime(begin, MPI_Wtime()) < TIMEOUT_TIME)) {
                        MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &flag, &status);

                        if (!flag) {
                            clearBuffer();
                            handleFullMessaging();
                        }
                    }

                    if (!flag) {
                        if (nRunning == 0) {
                            break; // Cancellation due to TIMEOUT
                        }
                    }
                }

                if (!flag) {
                    clearBuffer();
                    handleFullMessaging();
                    continue;
                }

                // at this point, probe succeeded => there is something to receive
                if (status.MPI_TAG == TASK_FOR_CENTER) {

                    MPI_Get_count(&status, MPI_CHAR, &buffer_char_count);
                    buffer_char = new char[buffer_char_count];
                    MPI_Recv(buffer_char, buffer_char_count, MPI_CHAR, status.MPI_SOURCE, status.MPI_TAG, world_Comm,
                             &status);
                } else {
                    MPI_Recv(&buffer, 1, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, world_Comm, &status);
                }

                switch (status.MPI_TAG) {
                case STATE_RUNNING: // received if and only if a worker receives from other but center
                {
                    processState[status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
                    ++nRunning;

                    ++totalRequests;
                }
                break;
                case STATE_AVAILABLE: {
                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("center received state_available from rank {}\n", status.MPI_SOURCE);
                    #endif
                    processState[status.MPI_SOURCE] = STATE_AVAILABLE;
                    ++nAvailable;
                    --nRunning;
                    ++totalRequests;
                }
                break;
                case REFVAL_UPDATE_TAG: {
                    /* if center reaches this point, for sure nodes have attained a better reference value
                            or they are not up-to-date, thus it is required to broadcast it whether this value
                            changes or not  */
                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("center received refValue {} from rank {}\n", buffer, status.MPI_SOURCE);
                    #endif
                    bool signal = false;

                    if ((maximisation && buffer > refValueGlobal) || (!maximisation && buffer < refValueGlobal)) {
                        // refValueGlobal[0] = buffer;
                        refValueGlobal = buffer;
                        signal = true;
                        for (int rank = 1; rank < world_size; rank++) {
                            MPI_Send(&refValueGlobal, 1, MPI_INT, rank, REFVAL_UPDATE_TAG, refValueGlobal_Comm);
                        }

                        // bcastPut(refValueGlobal, 1, MPI_INT, 0, win_refValueGlobal);
                    }

                    if (signal) {
                        static int success = 0;
                        success++;
                        spdlog::debug("refValueGlobal updated to : {} by rank {}\n", refValueGlobal,
                                      status.MPI_SOURCE);
                    } else {
                        static int failures = 0;
                        failures++;
                        spdlog::debug("FAILED updates : {}, refValueGlobal : {} by rank {}\n", failures,
                                      refValueGlobal, status.MPI_SOURCE);
                    }
                    ++totalRequests;
                }
                break;
                case TASK_FOR_CENTER: {

                    std::pair<char*, int> msg = std::make_pair(buffer_char, buffer_char_count);
                    //center_queue.push_back(msg);
                    center_queue.push(msg);

                    if (center_queue.size() > max_queue_size) {
                        if (center_queue.size() % 10000 == 0)
                            std::cout << "CENTER queue size reached " << center_queue.size() << std::endl;
                        max_queue_size = center_queue.size();
                    }

                    ++totalRequests;


                    if (center_queue.size() > 2 * CENTER_NBSTORED_TASKS_PER_PROCESS * world_size) {
                        if (difftime(time_centerfull_sent, MPI_Wtime() > 1)) {
                            spdlog::debug(
                                "Center queue size is twice the limit.  Contacting workers to let them know.\n");
                            center_last_full_status = false; //handleFullMessaging will see this and reontact workers
                        }
                    }

                    #ifdef GEMPBA_DEBUG_COMMENTS
                    spdlog::debug("center received task from {}, current queue size is {}\n", status.MPI_SOURCE, center_queue.size());
                    #endif
                }
                break;
                }

                clearBuffer();
                handleFullMessaging();
            }

            std::cout << "CENTER HAS TERMINATED" << std::endl;
            std::cout << "Max queue size = " << max_queue_size << ",   Peak memory (MB) = " << getPeakRSS() / (1024 * 1024)
                << std::endl;

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notifyTermination();

            // receive solution from other processes
            receiveSolution();

            end_time = MPI_Wtime();
        }

    private:
        /* return false if message not received, which is signal of termination
            all workers report (with a message) to center process when about to run a task or when becoming available
            if no message is received within a TIMEOUT window, then all processes will have finished
        */
        bool awaitMessage(int buffer, int& ready, double begin, MPI_Status& status, MPI_Request& request) {
            int cycles = 0;
            while (true) {
                MPI_Test(&request, &ready, &status);
                // Check whether the underlying communication had already taken place
                while (!ready && (difftime(begin, MPI_Wtime()) < TIMEOUT_TIME)) {
                    MPI_Test(&request, &ready, &status);
                    cycles++;
                }

                if (!ready) {
                    if (nRunning == 0) {
                        // Cancellation due to TIMEOUT
                        MPI_Cancel(&request);
                        MPI_Request_free(&request);
                        printf("rank %d: receiving TIMEOUT, buffer : %d, cycles : %d\n", world_rank, buffer, cycles);
                        return false;
                    }
                } else
                    return true;
            }
        }

        void notifyTermination() {
            for (int rank = 1; rank < world_size; rank++) {
                char buffer[] = "exit signal";
                int count = sizeof(buffer);
                MPI_Send(&buffer, count, MPI_CHAR, rank, TERMINATION_TAG, world_Comm); // send positive signal
            }
            MPI_Barrier(world_Comm);
        }

        int getAvailable() {
            for (int rank = 1; rank < world_size; rank++) {
                if (processState[rank] == STATE_AVAILABLE)
                    return rank;
            }
            return -1; // all nodes are running
        }

        /*	receive solution from nodes */
        void receiveSolution() {
            for (int rank = 1; rank < world_size; rank++) {

                MPI_Status status;
                int count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(rank, MPI_ANY_TAG, world_Comm, &status); // receives status before receiving the message
                MPI_Get_count(&status, MPI_CHAR,
                              &count); // receives total number of datatype elements of the message
                //***************************************************************************************************

                char* buffer = new char[count];
                MPI_Recv(buffer, count, MPI_CHAR, rank, MPI_ANY_TAG, world_Comm, &status);

                #ifdef GEMPBA_DEBUG_COMMENTS
                spdlog::debug("fetching result from rank {} \n", rank);
                #endif

                switch (status.MPI_TAG) {
                case HAS_RESULT_TAG: {
                    std::string buf(buffer, count);

                    int refValue;
                    MPI_Recv(&refValue, 1, MPI_INT, rank, HAS_RESULT_TAG, world_Comm, &status);

                    bestResults[rank].first = refValue; // reference value corresponding to result
                    bestResults[rank].second = buf; // best result so far from this rank

                    delete[] buffer;

                    spdlog::debug("solution received from rank {}, count : {}, refVal {} \n", rank, count, refValue);
                }
                break;

                case NO_RESULT_TAG: {
                    delete[] buffer;
                    spdlog::debug("solution NOT received from rank {}\n", rank);
                }
                break;
                }
            }
        }

        void sendSeed(const char* buffer, const int COUNT) {
            const int dest = 1;
            // global synchronisation **********************
            --nAvailable;
            processState[dest] = STATE_RUNNING;
            // *********************************************

            int err = MPI_Ssend(buffer, COUNT, MPI_CHAR, dest, TASK_FROM_CENTER_TAG, world_Comm); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

        void createCommunicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD,
                         &refValueGlobal_Comm); // exclusive communicator for reference value - one-sided comm

            MPI_Comm_dup(MPI_COMM_WORLD, &centerFullness_Comm);

            MPI_Comm_size(world_Comm, &this->world_size);
            MPI_Comm_rank(world_Comm, &this->world_rank);

            /*if (world_size < 2)
            {
                spdlog::debug("At least two processes required !!\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }*/
        }

        void allocateMPI() {
            MPI_Barrier(world_Comm);
            init();
            MPI_Barrier(world_Comm);
        }

        void deallocateMPI() {
            MPI_Comm_free(&refValueGlobal_Comm);
            MPI_Comm_free(&centerFullness_Comm);

            MPI_Comm_free(&world_Comm);
        }

        void init() {
            processState.resize(world_size, STATE_AVAILABLE);
            processTree.resize(world_size);
            max_queue_size = 0;

            refValueGlobal = INT_MIN;

            if (world_rank == 0)
                bestResults.resize(world_size, std::make_pair(-1, std::string()));

            transmitting = false;
        }

    private:
        int argc;
        char** argv;
        int world_rank; // get the rank of the process
        int world_size; // get the number of processes/nodes
        char processor_name[128]; // name of the node

        int nTasksRecvd = 0;
        int nTasksSent = 0;
        int nRunning = 0;
        int nAvailable = 0;
        std::vector<int> processState; // state of the nodes : running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree processTree;

        std::mutex mtx;
        std::atomic<bool> transmitting;
        int dest_rank_tmp = -1;

        Queue<std::string*> q;
        bool exit = false;

        // MPI_Group world_group;		  // all ranks belong to this group
        MPI_Comm refValueGlobal_Comm; // attached to win_refValueGlobal
        MPI_Comm centerFullness_Comm;

        MPI_Comm world_Comm; // world communicator


        int refValueGlobal;

        bool isCenterFull = false;

        bool maximisation = true; // true if maximising, false if minimising

        std::vector<std::pair<int, std::string>> bestResults;

        size_t threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        // statistics
        size_t totalRequests = 0;
        double start_time = 0;
        double end_time = 0;

        /* singleton*/
        MPI_SchedulerCentralized() {
            init(NULL, NULL);
        }

        void init(int* argc, char* argv[]) {
            // Initialise MPI and ask for thread support
            int provided;
            MPI_Init_thread(argc, &argv, MPI_THREAD_FUNNELED, &provided);

            if (provided < MPI_THREAD_FUNNELED) {
                spdlog::debug("The threading support level is lesser than that demanded.\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }

            createCommunicators();

            int namelen;
            MPI_Get_processor_name(processor_name, &namelen);
            spdlog::debug("Process {} of {} is on {}\n", world_rank, world_size, processor_name);
            allocateMPI();
        }

        void finalize() {
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, before deallocate \n", world_rank);
            #endif
            deallocateMPI();
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, after deallocate \n", world_rank);
            #endif
            MPI_Finalize();
            #ifdef GEMPBA_DEBUG_COMMENTS
            spdlog::debug("rank {}, after MPI_Finalize() \n", world_rank);
            #endif
        }
    };

}

#endif
