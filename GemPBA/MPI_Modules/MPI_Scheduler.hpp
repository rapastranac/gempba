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
#include <optional>
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
#define FUNCTION_ARGS_TAG 11

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
            return m_world_rank;
        }

        size_t getTotalRequests() const override {
            return m_total_requests_number;
        }

        void set_custom_initial_topology(tree&& p_tree) override {
            m_process_tree = std::move(p_tree);
            m_custom_initial_topology = true;
        }

        std::string fetchSolution() override {
            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_best_results[rank].first == m_global_reference_value) {
                    return m_best_results[rank].second;
                }
            }
            return {}; // no solution found
        }

        std::vector<std::pair<int, std::string>> fetchResVec() override {
            return m_best_results;
        }

        void printStats() override {
            spdlog::debug("\n \n \n");
            spdlog::debug("*****************************************************\n");
            spdlog::debug("Elapsed time : {:4.3f} \n", elapsedTime());
            spdlog::debug("Total number of requests : {} \n", m_total_requests_number);
            spdlog::debug("*****************************************************\n");
            spdlog::debug("\n \n \n");
        }

        double elapsedTime() const override {
            return (m_end_time - m_start_time) - static_cast<double>(TIMEOUT_TIME);
        }

        void allgather(void* recvbuf, void* sendbuf, MPI_Datatype mpi_datatype) override {
            MPI_Allgather(sendbuf, 1, mpi_datatype, recvbuf, 1, mpi_datatype, m_world_communicator);
            MPI_Barrier(m_world_communicator);
        }

        void gather(void* sendbuf, int sendcount, MPI_Datatype sendtype, void* recvbuf, int recvcount, MPI_Datatype recvtype, int root) override {
            MPI_Gather(sendbuf, sendcount, sendtype, recvbuf, recvcount, recvtype, root, m_world_communicator);
        }

        int getWorldSize() const override {
            return m_world_size;
        }

        int tasksRecvd() const override {
            return m_received_tasks;
        }

        int tasksSent() const override {
            return m_sent_tasks;
        }

        void barrier() override {
            if (m_world_communicator != MPI_COMM_NULL)
                MPI_Barrier(m_world_communicator);
        }

        bool openSendingChannel() override {
            if (m_mutex.try_lock()) // acquires mutex
            {
                if (!m_transmitting.load()) // check if transmission in progress
                {
                    int nxt = nextProcess();
                    if (nxt > 0) // check if there is another process in the list
                    {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                m_mutex.unlock();
            }
            return false;
        }

        /* this should be invoked only if priority is acquired*/
        void closeSendingChannel() override {
            m_mutex.unlock();
        }

        // returns the process rank assigned to receive a task from this process
        int nextProcess() const override {
            return this->m_next_process;
        }

        void setRefValStrategyLookup(bool maximisation) override {
            this->m_maximisation = maximisation;

            if (!maximisation) {
                // minimisation
                m_global_reference_value = INT_MAX;
            }
        }

    private:
        MPI_Status probe_communicators() {
            // this allows receiving refValue or nextProcess even if this process has turned into waiting mode
            while (true) {
                if (auto v_optional = probe_reference_value_comm(); v_optional.has_value()) {
                    return v_optional.value();
                }

                if (auto v_optional = probe_next_process_comm(); v_optional.has_value()) {
                    return v_optional.value();
                }

                if (const auto v_optional = probe_world_comm(); v_optional.has_value()) {
                    return v_optional.value();
                }
            }
        }

        void process_message(MPI_Status p_status, BranchHandler& p_branch_handler, std::function<std::shared_ptr<ResultHolderParent>(char*, int)>& p_buffer_decoder) {
            int v_count; // count to be received
            MPI_Get_count(&p_status, MPI_CHAR, &v_count); // receives total number of datatype elements of the message

            utils::print_mpi_debug_comments("rank {}, received message from rank {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, v_count);
            char* v_message = new char[v_count];
            MPI_Recv(v_message, v_count, MPI_CHAR, p_status.MPI_SOURCE, FUNCTION_ARGS_TAG, m_world_communicator, &p_status);

            notifyRunningState();
            m_received_tasks++;


            //  push to the thread pool *********************************************************************
            std::shared_ptr<ResultHolderParent> v_holder = p_buffer_decoder(v_message, v_count); // holder might be useful for non-void functions
            utils::print_mpi_debug_comments("rank {}, pushed buffer to thread pool \n", m_world_rank, p_status.MPI_SOURCE);
            // **********************************************************************************************

            taskFunneling(p_branch_handler);
            notifyAvailableState();

            delete[] v_message;
        }

        void process_termination(MPI_Status p_status) {
            constexpr int v_count = 1; // count to be received
            int v_unused;
            MPI_Recv(&v_unused, v_count, MPI_INT, p_status.MPI_SOURCE, TERMINATION_TAG, m_world_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received message from rank {}", m_world_rank, p_status.MPI_SOURCE);

            spdlog::debug("rank {} exited\n", m_world_rank);
            MPI_Barrier(m_world_communicator);
        }

    public:
        void runNode(BranchHandler& branchHandler, std::function<std::shared_ptr<ResultHolderParent>(char*, int)>& bufferDecoder,
                     std::function<std::pair<int, std::string>()>& resultFetcher) override {
            MPI_Barrier(m_world_communicator);
            // nice(18);

            bool v_is_terminated = false;
            while (true) {
                MPI_Status v_status = probe_communicators();

                switch (v_status.MPI_TAG) {
                case TERMINATION_TAG: {
                    process_termination(v_status);
                    v_is_terminated = true; // temporary, it should always happen
                    break;
                }
                case REFVAL_UPDATE_TAG: {
                    receive_reference_value(v_status);
                    break;
                }
                case NEXT_PROCESS_TAG: {
                    receive_next_process(v_status);
                    break;
                }
                case FUNCTION_ARGS_TAG: {
                    process_message(v_status, branchHandler, bufferDecoder);
                    break;
                }
                }

                if (v_is_terminated) {
                    break; // exit loop
                }
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
                auto str = fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank);
                throw std::runtime_error(str);
            }

            m_transmitting = true;
            m_destination_rank = nextProcess();
            utils::print_mpi_debug_comments("rank {} entered MPI_Scheduler::push(..) for the node {}\n", m_world_rank, m_destination_rank);
            shift_left(m_next_processes.data(), m_world_size);

            auto pck = std::make_shared<std::string>(std::forward<std::string&&>(message));
            auto _message = new std::string(*pck);

            if (!m_tasks_queue.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            m_tasks_queue.push(_message);
            closeSendingChannel();
        }

    private:
        void taskFunneling(BranchHandler& branchHandler);

        void receive_reference_value(MPI_Status p_status) {
            utils::print_mpi_debug_comments("rank {}, about to receive refValue from Center\n", m_world_rank);
            MPI_Recv(&m_global_reference_value, 1, MPI_INT, CENTER, REFVAL_UPDATE_TAG, m_global_reference_value_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received refValue: {} from Center\n", m_world_rank, m_global_reference_value);
        }

        std::optional<MPI_Status> probe_reference_value_comm() {
            int v_is_message_received = 0; // logical

            MPI_Status v_status;
            MPI_Iprobe(CENTER, REFVAL_UPDATE_TAG, m_global_reference_value_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                return v_status;

            }
            return std::nullopt;
        }

        // checks for a ref value update from center
        void maybe_receive_reference_value() {
            auto v_optional = probe_reference_value_comm();
            if (v_optional.has_value()) {
                receive_reference_value(v_optional.value());
            }
        }

        std::optional<MPI_Status> probe_world_comm() {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        void receive_next_process(MPI_Status p_status) {
            int v_count;
            MPI_Get_count(&p_status, MPI_INT, &v_count); // 0 < count < world_size   -- safe
            utils::print_mpi_debug_comments("rank {}, about to receive nextProcess from Center, count : {}\n", m_world_rank, v_count);
            MPI_Recv(m_next_processes.data(), v_count, MPI_INT, CENTER, NEXT_PROCESS_TAG, m_next_process_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received nextProcess from Center, count : {}\n", m_world_rank, v_count);
        }

        std::optional<MPI_Status> probe_next_process_comm() {
            int v_is_message_received = 0;

            MPI_Status v_status;
            MPI_Iprobe(CENTER, NEXT_PROCESS_TAG, m_next_process_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                return v_status;

            }
            return std::nullopt;
        }

        // checks for a new assigned process from center
        void maybe_receive_next_process() {
            auto v_optional = probe_next_process_comm();
            if (v_optional.has_value()) {
                receive_next_process(v_optional.value());
            }
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
            m_next_process = m_next_processes[0];
        }

        void notifyAvailableState() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, 0, STATE_AVAILABLE, m_world_communicator);
            utils::print_mpi_debug_comments("rank {} entered notifyAvailableState()\n", m_world_rank);
        }

        void notifyRunningState() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, 0, STATE_RUNNING, m_world_communicator);
        }

        void sendTask(std::string& message) {
            size_t messageLength = message.size();
            if (messageLength > std::numeric_limits<int>::max()) {
                throw std::runtime_error("message is to long to be sent in a single message, currently not supported");
            }

            if (m_destination_rank > 0) {
                if (m_destination_rank == m_world_rank) {
                    auto msg = "rank " + std::to_string(m_world_rank) + " attempting to send to itself !!!\n";
                    throw std::runtime_error(msg);
                }
                utils::print_mpi_debug_comments("rank {} about to send buffer to rank {}\n", m_world_rank, m_destination_rank);
                MPI_Send(message.data(), (int)messageLength, MPI_CHAR, m_destination_rank, FUNCTION_ARGS_TAG, m_world_communicator);
                utils::print_mpi_debug_comments("rank {} sent buffer to rank {}\n", m_world_rank, m_destination_rank);
                m_destination_rank = -1;
            } else {
                auto msg = "rank " + std::to_string(m_world_rank) + ", could not send task to rank " + std::to_string(m_destination_rank) + "\n";
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
                        m_process_tree[pi].add_next(q);
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
            for (int rank = 1; rank < m_world_size; rank++) {
                std::vector<int> buffer_tmp;
                for (auto& child : m_process_tree[rank]) {
                    buffer_tmp.push_back(child);

                    m_process_state[child] = STATE_ASSIGNED;
                    --m_nodes_available;
                }
                if (!buffer_tmp.empty()) {
                    MPI_Ssend(buffer_tmp.data(), (int)buffer_tmp.size(), MPI_INT, rank, NEXT_PROCESS_TAG, m_next_process_communicator);
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
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, CENTER, NO_RESULT_TAG, m_world_communicator);
            } else {
                MPI_Send(buffer.data(), buffer.size(), MPI_CHAR, CENTER, HAS_RESULT_TAG, m_world_communicator);
                MPI_Send(&refVal, 1, MPI_INT, CENTER, HAS_RESULT_TAG, m_world_communicator);
            }
        }

        /* it returns the substraction between end and start*/
        double difftime(double start, double end) {
            return end - start;
        }

        void process_running(MPI_Status p_status) {
            m_process_state[p_status.MPI_SOURCE] = STATE_RUNNING; // node was assigned, now it's running
            ++m_nodes_running;
            utils::print_mpi_debug_comments("rank {} reported running, nRunning :{}\n", p_status.MPI_SOURCE, m_nodes_running);

            if (m_process_tree[p_status.MPI_SOURCE].is_assigned())
                m_process_tree[p_status.MPI_SOURCE].release();

            if (!m_process_tree[p_status.MPI_SOURCE].has_next()) // checks if notifying node has a child to push to
            {
                int nxt = getAvailable();
                if (nxt > 0) {
                    // put(&nxt, 1, status.MPI_SOURCE, MPI_INT, 0, win_nextProcess);
                    MPI_Send(&nxt, 1, MPI_INT, p_status.MPI_SOURCE, NEXT_PROCESS_TAG, m_next_process_communicator);
                    m_process_tree[p_status.MPI_SOURCE].add_next(nxt);
                    m_process_state[nxt] = STATE_ASSIGNED;
                    --m_nodes_available;
                }
            }
            m_total_requests_number++;
        }

        void process_available(MPI_Status p_status) {
            m_process_state[p_status.MPI_SOURCE] = STATE_AVAILABLE;
            ++m_nodes_available;
            --m_nodes_running;

            if (m_process_tree[p_status.MPI_SOURCE].is_assigned()) {
                m_process_tree[p_status.MPI_SOURCE].release();
            }
            utils::print_mpi_debug_comments("rank {} reported available, nRunning :{}\n", p_status.MPI_SOURCE, m_nodes_running);
            std::random_device v_rd; // Will be used to obtain a seed for the random number engine
            std::mt19937 v_gen(v_rd()); // Standard mersenne_twister_engine seeded with rd()
            std::uniform_int_distribution<> v_distrib(0, m_world_size); // uniform distribution [closed interval]
            const int v_random_offset = v_distrib(v_gen); // random value in range

            for (int i = 0; i < m_world_size; i++) {
                int v_rank = (i + v_random_offset) % m_world_size; // this ensures that rank is always in range 0 <= rank < world_size
                if (v_rank <= 0) {
                    continue;
                }

                // finds the first running node
                if (m_process_state[v_rank] != STATE_RUNNING) {
                    continue;
                }

                // checks if running node has a child to push to
                if (m_process_tree[v_rank].has_next()) {
                    continue;
                }

                MPI_Send(&p_status.MPI_SOURCE, 1, MPI_INT, v_rank, NEXT_PROCESS_TAG, m_next_process_communicator);

                m_process_tree[v_rank].add_next(p_status.MPI_SOURCE); // assigns the returning node as a running node
                m_process_state[p_status.MPI_SOURCE] = STATE_ASSIGNED; // it flags the returning node as assigned
                --m_nodes_available; // assigned, not available anymore
                utils::print_mpi_debug_comments("ASSIGNMENT:\trank {} <-- [{}]\n", v_rank, p_status.MPI_SOURCE);
                break; // IMPORTANT: breaks for-loop
            }
        }

        /**
        * if center reaches this point, for sure workers have attained a better reference value,
        * or they are not up-to-date, thus it is required to broadcast it whether this value changes or not
        */
        void maybe_broadcast_global_reference_value(int p_new_global_reference_value, MPI_Status p_status) {
            utils::print_mpi_debug_comments("center received refValue {} from rank {}\n", p_new_global_reference_value, p_status.MPI_SOURCE);
            bool v_reference_value_updated = false;

            if ((m_maximisation && p_new_global_reference_value > m_global_reference_value) || (!m_maximisation && p_new_global_reference_value < m_global_reference_value)) {
                m_global_reference_value = p_new_global_reference_value;
                v_reference_value_updated = true;
                for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                    MPI_Send(&m_global_reference_value, 1, MPI_INT, v_rank, REFVAL_UPDATE_TAG, m_global_reference_value_communicator);
                }
            }

            if (v_reference_value_updated) {
                static int success = 0;
                success++;
                spdlog::info("SUCCESSFUL updates: {}, refValueGlobal updated to : {} by rank {}\n", success, m_global_reference_value, p_status.MPI_SOURCE);
            } else {
                static int failures = 0;
                failures++;
                spdlog::debug("FAILED updates : {}, refValueGlobal : {} by rank {}\n", failures, m_global_reference_value, p_status.MPI_SOURCE);
            }
        }

    public:
        /*	run the center node */
        void runCenter(const char* p_seed, const int p_seed_size) override {
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();

            if (!m_custom_initial_topology) {
                build_topology(1, 0, 2, m_world_size);
            }
            broadcast_nodes_topology();

            send_seed(p_seed, p_seed_size);


            while (true) {
                int buffer;
                MPI_Status status;
                MPI_Request request;
                int ready;
                const double v_wall_time0 = MPI_Wtime();
                MPI_Irecv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &request);

                const bool v_timeout = receive_or_timeout(buffer, ready, v_wall_time0, status, request);
                if (v_timeout)
                    break;

                switch (status.MPI_TAG) {
                case STATE_RUNNING: {
                    // received if and only if a worker receives from other but center
                    process_running(status);
                    break;
                }
                case STATE_AVAILABLE: {
                    process_available(status);
                    break;
                }
                case REFVAL_UPDATE_TAG: {
                    maybe_broadcast_global_reference_value(buffer, status);
                    break;
                }
                }
            }

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notifyTermination();

            // receive solution from other processes
            receiveSolution();

            m_end_time = MPI_Wtime();
        }

    private:
        /* return false if message not received, which is signal of termination
            all workers report (with a message) to center process when about to run a task or when becoming available
            if no message is received within a TIMEOUT window, then all processes will have finished
        */
        bool receive_or_timeout(int p_buffer, int& p_ready, double p_wall_time0, MPI_Status& p_status, MPI_Request& p_request) {
            int v_cycles = 0;
            while (true) {
                MPI_Test(&p_request, &p_ready, &p_status);
                // Check whether the underlying communication had already taken place
                while (!p_ready && (TIMEOUT_TIME > difftime(p_wall_time0, MPI_Wtime()))) {
                    MPI_Test(&p_request, &p_ready, &p_status);
                    v_cycles++;
                }

                if (p_ready) {
                    return false;
                }
                if (m_nodes_running == 0) {
                    // Cancellation due to TIMEOUT
                    MPI_Cancel(&p_request);
                    MPI_Request_free(&p_request);
                    spdlog::info("rank {}: receiving TIMEOUT, buffer : {}, cycles : {}\n", m_world_rank, p_buffer, v_cycles);
                    return true;
                }
            }
        }

        void notifyTermination() {
            for (int rank = 1; rank < m_world_size; rank++) {
                constexpr int v_buffer = 0;
                constexpr int v_count = 1;
                MPI_Send(&v_buffer, v_count, MPI_INT, rank, TERMINATION_TAG, m_world_communicator); // send positive signal
            }
            MPI_Barrier(m_world_communicator);
        }

        int getAvailable() {
            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_process_state[rank] == STATE_AVAILABLE)
                    return rank;
            }
            return -1; // all nodes are running
        }

        /*	receive solution from nodes */
        void receiveSolution() {
            for (int rank = 1; rank < m_world_size; rank++) {

                MPI_Status status;
                int count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(rank, MPI_ANY_TAG, m_world_communicator, &status); // receives status before receiving the message
                MPI_Get_count(&status, MPI_CHAR,
                              &count); // receives total number of datatype elements of the message
                //***************************************************************************************************

                char* buffer = new char[count];
                MPI_Recv(buffer, count, MPI_CHAR, rank, MPI_ANY_TAG, m_world_communicator, &status);

                utils::print_mpi_debug_comments("fetching result from rank {} \n", rank);

                switch (status.MPI_TAG) {
                case HAS_RESULT_TAG: {
                    std::string buf(buffer, count);

                    int refValue;
                    MPI_Recv(&refValue, 1, MPI_INT, rank, HAS_RESULT_TAG, m_world_communicator, &status);

                    m_best_results[rank].first = refValue; // reference value corresponding to result
                    m_best_results[rank].second = buf; // best result so far from this rank

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

        void send_seed(const char* buffer, const int COUNT) {
            const int dest = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[dest] = STATE_RUNNING;
            // *********************************************

            int err = MPI_Ssend(buffer, COUNT, MPI_CHAR, dest, FUNCTION_ARGS_TAG, m_world_communicator); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

        void createCommunicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &m_world_communicator); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD,
                         &m_global_reference_value_communicator); // exclusive communicator for reference value - one-sided comm
            MPI_Comm_dup(MPI_COMM_WORLD,
                         &m_next_process_communicator); // exclusive communicator for next process - one-sided comm

            MPI_Comm_size(m_world_communicator, &this->m_world_size);
            MPI_Comm_rank(m_world_communicator, &this->m_world_rank);

            /*if (world_size < 2)
            {
                spdlog::debug("At least two processes required !!\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }*/
        }

        void allocateMPI() {
            MPI_Barrier(m_world_communicator);
            init();
            MPI_Barrier(m_world_communicator);
        }

        void deallocateMPI() {
            MPI_Comm_free(&m_global_reference_value_communicator);
            MPI_Comm_free(&m_next_process_communicator);
            MPI_Comm_free(&m_world_communicator);
        }

        void init() {
            m_process_state.resize(m_world_size, STATE_AVAILABLE);
            m_process_tree.resize(m_world_size);
            m_next_processes.resize(m_world_size, -1);
            m_global_reference_value = INT_MIN;

            if (m_world_rank == 0)
                m_best_results.resize(m_world_size, std::make_pair(-1, std::string()));

            m_transmitting = false;
        }

    private:
        int m_argc;
        char** m_argv;
        int m_world_rank; // get the rank of the process
        int m_world_size; // get the number of processes/nodes
        char m_processor_name[128]; // name of the node

        int m_received_tasks = 0;
        int m_sent_tasks = 0;
        int m_nodes_running = 0;
        int m_nodes_available = 0;
        std::vector<int> m_process_state; // state of the nodes: running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree m_process_tree;

        std::mutex m_mutex;
        std::atomic<bool> m_transmitting;
        int m_destination_rank = -1;

        Queue<std::string*> m_tasks_queue;

        MPI_Comm m_global_reference_value_communicator; // BIDIRECTIONAL
        MPI_Comm m_next_process_communicator; // CENTER TO WORKER (ONLY)
        MPI_Comm m_world_communicator; // BIDIRECTIONAL

        std::vector<int> m_next_processes;
        int m_global_reference_value;

        int m_next_process = -1;

        bool m_maximisation = true; // true if maximising, false if minimising

        std::vector<std::pair<int, std::string>> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        // statistics
        size_t m_total_requests_number = 0;
        double m_start_time = 0;
        double m_end_time = 0;

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
            MPI_Get_processor_name(m_processor_name, &namelen);
            spdlog::debug("Process {} of {} is on {}\n", m_world_rank, m_world_size, m_processor_name);
            allocateMPI();
        }

        void finalize() {
            utils::print_mpi_debug_comments("rank {}, before deallocate \n", m_world_rank);
            deallocateMPI();
            utils::print_mpi_debug_comments("rank {}, after deallocate \n", m_world_rank);
            MPI_Finalize();
            utils::print_mpi_debug_comments("rank {}, after MPI_Finalize() \n", m_world_rank);
        }
    };

}

#endif
