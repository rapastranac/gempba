#pragma once
#ifndef MPI_SCHEDULER_HPP
#define MPI_SCHEDULER_HPP

#include "scheduler_parent.hpp"
#include "utils/Queue.hpp"
#include "utils/tree.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <cstdlib> /* srand, rand */
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <mpi.h>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

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

namespace gempba {
    class BranchHandler;

    class ResultHolderParent;

    // inter process communication handler
    class MPI_Scheduler final : public SchedulerParent {

    public:
        ~MPI_Scheduler() override {
            finalize();
        }

        static MPI_Scheduler& getInstance() {
            static MPI_Scheduler instance;
            return instance;
        }

        int rank_me() const {
            return world_rank;
        }

        size_t getTotalRequests() const override {
            return totalRequests;
        }

        void set_custom_initial_topology(tree&& p_tree) override {
            processTree = std::move(p_tree);
            m_custom_initial_topology = true;
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

        double elapsedTime() const override {
            return (end_time - start_time) - static_cast<double>(TIMEOUT_TIME);
        }

        void allgather(void* recvbuf, void* sendbuf, MPI_Datatype mpi_datatype) override {
            MPI_Allgather(sendbuf, 1, mpi_datatype, recvbuf, 1, mpi_datatype, world_Comm);
            MPI_Barrier(world_Comm);
        }

        void gather(void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, int root) override {
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
                    int nxt = nextProcess();
                    if (nxt > 0) // check if there is another process in the list
                    {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                mtx.unlock();
            }
            return false;
        }

        /* this should be invoked only if priority is acquired*/
        void closeSendingChannel() override {
            mtx.unlock();
        }

        // returns the process rank assigned to receive a task from this process
        int nextProcess() const override {
            return this->nxtProcess;
        }

        void setRefValStrategyLookup(bool maximisation) override {
            this->maximisation = maximisation;

            if (!maximisation) {
                // minimisation
                refValueGlobal = INT_MAX;
            }
        }


        void runNode(BranchHandler& branchHandler, std::function<std::shared_ptr<ResultHolderParent>(char*, int)>& bufferDecoder,
                     std::function<std::pair<int, std::string>()>& resultFetcher) override {
            MPI_Barrier(world_Comm);
            // nice(18);

            while (true) {
                MPI_Status status;
                int count; // count to be received
                int flag = 0;

                // this allows receiving refValue or nextProcess even if this process has turned into waiting mode
                while (!flag) {
                    if (probe_refValue()) // different communicator
                        continue; // center might update this value even if this process is idle

                    if (probe_nextProcess()) // different communicator
                        continue; // center might update this value even if this process is idle

                    MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &flag, &status); // for regular messages
                    if (flag)
                        break;
                }
                MPI_Get_count(&status, MPI_CHAR, &count); // receives total number of datatype elements of the message

                utils::print_mpi_debug_comments("rank {}, received message from rank {}, count : {}\n", world_rank, status.MPI_SOURCE, count);
                char* message = new char[count];
                MPI_Recv(message, count, MPI_CHAR, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &status);

                if (isTerminated(status.MPI_TAG)) {
                    delete[] message;
                    break;
                }

                notifyRunningState();
                nTasksRecvd++;

                //  push to the thread pool *********************************************************************
                std::shared_ptr<ResultHolderParent> holder = bufferDecoder(message, count); // holder might be useful for non-void functions
                utils::print_mpi_debug_comments("rank {}, pushed buffer to thread pool \n", world_rank, status.MPI_SOURCE);
                // **********************************************************************************************

                taskFunneling(branchHandler);
                notifyAvailableState();

                delete[] message;
            }
            /**
             * TODO.. send results back to the rank from which the task was sent.
             * this applies only when parallelising non-void functions
             */

            sendSolution(resultFetcher);
        }

        /* enqueue a message which will be sent to the next assigned process
            message pushing is only possible when the preceding message has been successfully pushed
            to another process, to avoid enqueuing.
        */
        void push(std::string&& message) override {
            if (message.empty()) {
                auto str = fmt::format("rank {}, attempted to send empty buffer \n", world_rank);
                throw std::runtime_error(str);
            }

            transmitting = true;
            dest_rank_tmp = nextProcess();
            utils::print_mpi_debug_comments("rank {} entered MPI_Scheduler::push(..) for the node {}\n", world_rank, dest_rank_tmp);
            shift_left(next_process.data(), world_size);

            auto pck = std::make_shared<std::string>(std::forward<std::string&&>(message));
            auto _message = new std::string(*pck);

            if (!q.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            q.push(_message);
            closeSendingChannel();
        }

    private:
        void taskFunneling(BranchHandler& branchHandler);

        // checks for a ref value update from center
        int probe_refValue() {
            int flag = 0;
            MPI_Status status;
            MPI_Iprobe(CENTER, REFVAL_UPDATE_TAG, refValueGlobal_Comm, &flag, &status);

            if (flag) {
                utils::print_mpi_debug_comments("rank {}, about to receive refValue from Center\n", world_rank);
                MPI_Recv(&refValueGlobal, 1, MPI_INT, CENTER, REFVAL_UPDATE_TAG, refValueGlobal_Comm, &status);
                utils::print_mpi_debug_comments("rank {}, received refValue: {} from Center\n", world_rank, refValueGlobal);
            }

            return flag;
        }

        // checks for a new assigned process from center
        int probe_nextProcess() {
            int flag = 0;
            MPI_Status status;
            int count;
            MPI_Iprobe(CENTER, NEXT_PROCESS_TAG, nextProcess_Comm, &flag, &status);

            if (flag) {
                MPI_Get_count(&status, MPI_INT, &count); // 0 < count < world_size   -- safe
                utils::print_mpi_debug_comments("rank {}, about to receive nextProcess from Center, count : {}\n", world_rank, count);
                MPI_Recv(next_process.data(), count, MPI_INT, CENTER, NEXT_PROCESS_TAG, nextProcess_Comm, &status);
                utils::print_mpi_debug_comments("rank {}, received nextProcess from Center, count : {}\n", world_rank, count);
            }

            return flag;
        }

        // if ref value received, it attempts updating local value
        // if local value is better than the one in center, then the local best value is sent to center
        void updateRefValue(BranchHandler& branchHandler);

        /*	- return true is priority is acquired, false otherwise
            - priority released automatically if a message is pushed, otherwise it should be released manually
            - only ONE buffer will be enqueued at a time
            - if the taskFunneling is transmitting the buffer to another node, this method will return false
            - if previous conditions are met, then the actual condition for pushing is evaluated next_process[0] > 0
        */

        // it separates receiving buffer from the local
        void updateNextProcess() {
            nxtProcess = next_process[0];
        }

        bool isTerminated(int TAG) {
            if (TAG == TERMINATION_TAG) {
                spdlog::debug("rank {} exited\n", world_rank);
                MPI_Barrier(world_Comm);
                return true;
            }
            return false;
        }

        void notifyAvailableState() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, 0, STATE_AVAILABLE, world_Comm);
            utils::print_mpi_debug_comments("rank {} entered notifyAvailableState()\n", world_rank);
        }

        void notifyRunningState() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, 0, STATE_RUNNING, world_Comm);
        }

        void sendTask(std::string& message) {
            size_t messageLength = message.size();
            if (messageLength > std::numeric_limits<int>::max()) {
                throw std::runtime_error("message is to long to be sent in a single message, currently not supported");
            }

            if (dest_rank_tmp > 0) {
                if (dest_rank_tmp == world_rank) {
                    auto msg = "rank " + std::to_string(world_rank) + " attempting to send to itself !!!\n";
                    throw std::runtime_error(msg);
                }
                utils::print_mpi_debug_comments("rank {} about to send buffer to rank {}\n", world_rank, dest_rank_tmp);
                MPI_Send(message.data(), (int)messageLength, MPI_CHAR, dest_rank_tmp, 0, world_Comm);
                utils::print_mpi_debug_comments("rank {} sent buffer to rank {}\n", world_rank, dest_rank_tmp);
                dest_rank_tmp = -1;
            } else {
                auto msg = "rank " + std::to_string(world_rank) + ", could not send task to rank " + std::to_string(dest_rank_tmp) + "\n";
                throw std::runtime_error(msg);
            }
        }

        /* shift a position to left of an array, leaving -1 as default value*/
        static void shift_left(int v[], const int size) {
            for (int i = 0; i < (size - 1); i++) {
                if (v[i] != -1) {
                    v[i] = v[i + 1]; // shift one cell to the left
                } else {
                    break; // no more data
                }
            }
        }

        void build_topology(int pi, int base_d, int b, int p) {
            for (int depth = base_d; depth < log2(p); depth++) {
                for (int j = 1; j < b; j++) {
                    int q = getNextProcess(j, pi, b, depth);
                    if (q < p && q > 0) {
                        processTree[pi].add_next(q);
                        spdlog::debug("process: {}, child: {}\n", pi, q);
                        build_topology(q, depth + 1, b, p);
                    }
                }
            }
        }

    public:
    private:
        /*	each node has an array containing its children were it is going to send tasks,
            this method puts the rank of these nodes into the array in the order that they
            are supposed to help the parent
        */
        void broadcast_nodes_topology() {
            // send initial topology to each process
            for (int rank = 1; rank < world_size; rank++) {
                std::vector<int> buffer_tmp;
                for (auto& child : processTree[rank]) {
                    buffer_tmp.push_back(child);

                    processState[child] = STATE_ASSIGNED;
                    --nAvailable;
                }
                if (!buffer_tmp.empty()) {
                    MPI_Ssend(buffer_tmp.data(), (int)buffer_tmp.size(), MPI_INT, rank, NEXT_PROCESS_TAG, nextProcess_Comm);
                }
            }
        }

        // already adapted for multibranching
        static int getNextProcess(int j, int pi, int b, int depth) {
            return (j * pow(b, depth)) + pi;
        }

        /*	send a solution achieved from node to the center node */
        void sendSolution(auto&& resultFetcher) {
            auto [refVal, buffer] = resultFetcher();
            if (buffer.starts_with("Empty")) {
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, CENTER, NO_RESULT_TAG, world_Comm);
            } else {
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, CENTER, HAS_RESULT_TAG, world_Comm);
                MPI_Send(&refVal, 1, MPI_INT, CENTER, HAS_RESULT_TAG, world_Comm);
            }
        }

        /* it returns the substraction between end and start*/
        double difftime(double start, double end) {
            return end - start;
        }

    public:
        /*	run the center node */
        void runCenter(const char* p_seed, const int p_seed_size) override {
            MPI_Barrier(world_Comm);
            start_time = MPI_Wtime();

            if (!m_custom_initial_topology) {
                build_topology(1, 0, 2, world_size);
            }
            broadcast_nodes_topology();

            send_seed(p_seed, p_seed_size);

            int rcv_availability = 0;

            while (true) {
                int buffer;
                MPI_Status status;
                MPI_Request request;
                int ready;
                double begin = MPI_Wtime();
                MPI_Irecv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, world_Comm, &request);

                if (!awaitMessage(buffer, ready, begin, status, request))
                    break;

                switch (status.MPI_TAG) {
                case STATE_RUNNING: // received if and only if a worker receives from other but center
                {
                    processState[status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
                    ++nRunning;
                    utils::print_mpi_debug_comments("rank {} reported running, nRunning :{}\n", status.MPI_SOURCE, nRunning);

                    if (processTree[status.MPI_SOURCE].is_assigned())
                        processTree[status.MPI_SOURCE].release();

                    if (!processTree[status.MPI_SOURCE].has_next()) // checks if notifying node has a child to push to
                    {
                        int nxt = getAvailable();
                        if (nxt > 0) {
                            // put(&nxt, 1, status.MPI_SOURCE, MPI_INT, 0, win_nextProcess);
                            MPI_Send(&nxt, 1, MPI_INT, status.MPI_SOURCE, NEXT_PROCESS_TAG, nextProcess_Comm);
                            processTree[status.MPI_SOURCE].add_next(nxt);
                            processState[nxt] = STATE_ASSIGNED;
                            --nAvailable;
                        }
                    }
                    totalRequests++;
                }
                break;
                case STATE_AVAILABLE: {
                    ++rcv_availability;
                    processState[status.MPI_SOURCE] = STATE_AVAILABLE;
                    ++nAvailable;
                    --nRunning;

                    if (processTree[status.MPI_SOURCE].is_assigned()) {
                        processTree[status.MPI_SOURCE].release();
                    }
                    utils::print_mpi_debug_comments("rank {} reported available, nRunning :{}\n", status.MPI_SOURCE, nRunning);
                    std::random_device rd; // Will be used to obtain a seed for the random number engine
                    std::mt19937 gen(
                        rd()); // Standard mersenne_twister_engine seeded with rd()
                    std::uniform_int_distribution<> distrib(0,
                                                            world_size); // uniform distribution [closed interval]
                    int random_offset = distrib(gen); // random value in range

                    for (int i_rank = 0; i_rank < world_size; i_rank++) {
                        int rank = (i_rank + random_offset) %
                            world_size; // this ensures that rank is always in range 0 <= rank < world_size
                        if (rank > 0) {
                            if (processState[rank] == STATE_RUNNING) // finds the first running node
                            {
                                if (!processTree[rank].has_next()) // checks if running node has a child to push to
                                {
                                    MPI_Send(&status.MPI_SOURCE, 1, MPI_INT, rank, NEXT_PROCESS_TAG,
                                             nextProcess_Comm);

                                    processTree[rank].add_next(
                                        status.MPI_SOURCE); // assigns returning node to the running node
                                    processState[status.MPI_SOURCE] = STATE_ASSIGNED; // it flags returning node as assigned
                                    --nAvailable; // assigned, not available any more
                                    utils::print_mpi_debug_comments("ASSIGNEMENT:\trank {} <-- [{}]\n", rank, status.MPI_SOURCE);
                                    break; // breaks for-loop
                                }
                            }
                        }
                    }
                }
                break;
                case REFVAL_UPDATE_TAG: {
                    /* if center reaches this point, for sure nodes have attained a better reference value
                            or they are not up-to-date, thus it is required to broadcast it whether this value
                            changes or not  */
                    utils::print_mpi_debug_comments("center received refValue {} from rank {}\n", buffer, status.MPI_SOURCE);
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
                        spdlog::debug("refValueGlobal updated to : {} by rank {}\n", refValueGlobal, status.MPI_SOURCE);
                    } else {
                        static int failures = 0;
                        failures++;
                        spdlog::debug("FAILED updates : {}, refValueGlobal : {} by rank {}\n", failures, refValueGlobal, status.MPI_SOURCE);
                    }
                }
                break;
                }
            }

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

                utils::print_mpi_debug_comments("fetching result from rank {} \n", rank);

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

        void send_seed(const char *buffer, const int COUNT) {
            const int dest = 1;
            // global synchronisation **********************
            --nAvailable;
            processState[dest] = STATE_RUNNING;
            // *********************************************

            int err = MPI_Ssend(buffer, COUNT, MPI_CHAR, dest, 0, world_Comm); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

        void createCommunicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &world_Comm); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD,
                         &refValueGlobal_Comm); // exclusive communicator for reference value - one-sided comm
            MPI_Comm_dup(MPI_COMM_WORLD,
                         &nextProcess_Comm); // exclusive communicator for next process - one-sided comm

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
            MPI_Comm_free(&nextProcess_Comm);
            MPI_Comm_free(&world_Comm);
        }

        void init() {
            processState.resize(world_size, STATE_AVAILABLE);
            processTree.resize(world_size);
            next_process.resize(world_size, -1);
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
        std::vector<int> processState; // state of the nodes: running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree processTree;

        std::mutex mtx;
        std::atomic<bool> transmitting;
        int dest_rank_tmp = -1;

        Queue<std::string*> q;
        bool exit = false;

        // MPI_Group world_group;	// all ranks belong to this group
        MPI_Comm refValueGlobal_Comm; // attached to win_refValueGlobal
        MPI_Comm nextProcess_Comm; // attached to win_nextProcess
        MPI_Comm world_Comm; // world communicator

        std::vector<int> next_process;
        int refValueGlobal;

        int nxtProcess = -1;

        bool maximisation = true; // true if maximising, false if minimising

        std::vector<std::pair<int, std::string>> bestResults;

        size_t threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        // statistics
        size_t totalRequests = 0;
        double start_time = 0;
        double end_time = 0;

        /* singleton*/
        MPI_Scheduler() {
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
            utils::print_mpi_debug_comments("rank {}, before deallocate \n", world_rank);
            deallocateMPI();
            utils::print_mpi_debug_comments("rank {}, after deallocate \n", world_rank);
            MPI_Finalize();
            utils::print_mpi_debug_comments("rank {}, after MPI_Finalize() \n", world_rank);
        }
    };

}

#endif
