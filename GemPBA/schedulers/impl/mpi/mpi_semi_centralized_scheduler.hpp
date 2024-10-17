#pragma once
#ifndef GEMPBA_MPISCHEDULER_H
#define GEMPBA_MPISCHEDULER_H

#include "branch_management/node_manager.hpp"
#include "schedulers/api/scheduler.hpp"
#include "utils/Tree.hpp"
#include "utils/Queue.hpp"

#include <atomic>
#include <functional>
#include <climits>
#include <map>
#include <memory>
#include <mpi.h>
#include <mutex>
#include <random>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

#define TIMEOUT_TIME 3


/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class NodeManager;

    class MPISemiCentralizedScheduler final : public Scheduler {

        // Scheduler-specific variables
        LookupStrategy _lookup_strategy{};
        int _global_reference_value{};

        std::mutex _mutex;
        volatile std::atomic<bool> _transmitting;
        int _destination_rank = -1;
        Queue<std::shared_ptr<std::pair<std::string, int>>> _tasks_queue;


        int _non_void_source_rank = -1; // this is only used for the non-void functions
        std::shared_future<std::string> _returned_value_future;

        int _nodes_running = 0;
        int _nodes_available = 0;
        std::vector<Tags> _process_state; // state of the nodes: running, assigned or available
        Tree _process_tree;


        // statistics
        bool _synchronized = false;
        std::vector<int> _received_tasks_per_process{};
        std::vector<int> _sent_tasks_per_process{};
        std::vector<double> _idle_time_per_process{};

        int _received_tasks = 0;
        int _sent_tasks = 0;
        size_t _total_requests_number = 0;
        double _start_time = 0;
        double _end_time = 0;
        double _idle_time = 0;

        // MPI specific variables
        MPI_Comm _global_reference_value_communicator{}; // attached to win__global_reference_value
        MPI_Comm _next_process_communicator{};      // attached to win_nextProcess
        MPI_Comm _world_communicator{};          // world communicator
        int _world_rank{};          // get the rank of the process
        int _world_size{};          // get the number of processes/nodes
        char processor_name[128]{}; // name of the node

        std::vector<int> _next_processes;
        int _next_process = -1;
        std::map<int, std::pair<int, std::string>, std::less<>> _best_results_map;


        // Private constructor for singleton
        MPISemiCentralizedScheduler() {
            init_mpi();
            init_member_variables();
            spdlog::info("MPI Scheduler, instantiated!\n");
        }

    public:
        // Singleton instance creation
        static MPISemiCentralizedScheduler *getInstance() {
            static auto *instance = new MPISemiCentralizedScheduler();
            return instance;
        }

        ~MPISemiCentralizedScheduler() override {
            // MPI specific finalization
            deallocateMPI();
            spdlog::info("Goodbye from MPI Scheduler\n");
        }

        MPISemiCentralizedScheduler(const MPISemiCentralizedScheduler &) = delete;

        void operator=(const MPISemiCentralizedScheduler &) = delete;

    public:
        void runCenter(const char *seed, int count) override {
            MPI_Barrier(_world_communicator);
            _start_time = MPI_Wtime();

            utils::build_topology(_process_tree, 1, 0, 2, _world_size);
            broadcast_nodes_topology();
            send_seed(seed, count);

            while (true) {
                int buffer;
                MPI_Status status;
                MPI_Request request;
                int ready;
                double wTime0 = MPI_Wtime();
                MPI_Irecv(&buffer, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, _world_communicator, &request);

                bool timeout = receiveOrTimeout(buffer, ready, wTime0, status, request);
                if (timeout) {
                    break;
                }

                Tags tag = static_cast<Tags>(status.MPI_TAG);
                switch (tag) {
                    case RUNNING_STATE: {
                        process_running(status);
                        break;
                    }
                    case AVAILABLE_STATE: {
                        process_available(status);
                        break;
                    }
                    case REFERENCE_VAL_UPDATE: {
                        maybe_broadcast_global_reference_value(buffer, status);
                        break;
                    }
                    default:
                        break;
                }
            }

            notify_termination();
            receive_solution();

            _end_time = MPI_Wtime();

            synchronize_statistics();
        }

        void runNode(NodeManager &nodeManager) override {
            MPI_Barrier(_world_communicator);

            bool is_finished = false;
            while (true) {
                MPI_Status status = probe_new_message();

                Tags tag = static_cast<Tags>(status.MPI_TAG);
                switch (tag) {
                    case FUNCTION_ARGS: {
                        process_message(nodeManager);
                        break;
                    }
                    case TERMINATION: {
                        process_termination();
                        is_finished = true;
                        break;
                    }
                    default:
                        break;
                }

                if (is_finished) {
                    break;
                }
            }

            send_final_solution_to_center(nodeManager);
            _idle_time = nodeManager.idle_time();
            synchronize_statistics();
        }

        void synchronize_statistics() override {
            barrier();

            if (_world_rank == CENTER_NODE) {
                _received_tasks_per_process.resize(_world_size);
                _sent_tasks_per_process.resize(_world_size);
                _idle_time_per_process.resize(_world_size);
                _received_tasks_per_process[0] = _received_tasks;
                _sent_tasks_per_process[0] = _sent_tasks;
                _idle_time_per_process[0] = _idle_time;

                int code1 = MPI_Gather(MPI_IN_PLACE, 1, MPI_INT, _received_tasks_per_process.data(), 1, MPI_INT, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather received tasks", code1);
                int code2 = MPI_Gather(MPI_IN_PLACE, 1, MPI_INT, _sent_tasks_per_process.data(), 1, MPI_INT, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather sent tasks", code2);
                int code3 = MPI_Gather(MPI_IN_PLACE, 1, MPI_DOUBLE, _idle_time_per_process.data(), 1, MPI_DOUBLE, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather idle time", code3);
            } else {
                int code1 = MPI_Gather(&_received_tasks, 1, MPI_INT, NULL, 1, MPI_INT, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather received tasks", code1);
                int code2 = MPI_Gather(&_sent_tasks, 1, MPI_INT, NULL, 1, MPI_INT, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather sent tasks", code2);
                int code3 = MPI_Gather(&_idle_time, 1, MPI_DOUBLE, NULL, 1, MPI_DOUBLE, CENTER_NODE, _world_communicator);
                check_MPI_error("Failed to gather idle time", code3);
            }
            barrier();

            _synchronized = true;
            spdlog::info("Synchronization complete");
        }

        void setLookupStrategy(LookupStrategy strategy) override {
            _lookup_strategy = strategy;
        }

        [[nodiscard]] int rank_me() const override {
            return _world_rank;
        }

        [[nodiscard]] int world_size() const override {
            return _world_size;
        }

        void barrier() override {
            if (_world_communicator == MPI_COMM_NULL) {
                spdlog::throw_spdlog_ex("Attempting to call barrier on MPI_COMM_NULL");
            }
            MPI_Barrier(_world_communicator);
        }

        [[nodiscard]] size_t get_received_task_count(int rank) const override {
            if (_synchronized && _world_rank == 0) {
                return _received_tasks_per_process[rank];
            }
            spdlog::warn("get_received_task_count() called before synchronization, returning local value for this rank: {}\n", _world_rank);
            return _received_tasks;
        }

        [[nodiscard]] size_t get_sent_task_count(int rank) const override {
            if (_synchronized && _world_rank == 0) {
                return _sent_tasks_per_process[rank];
            }
            spdlog::warn("get_sent_task_count() called before synchronization, returning local value for this rank: {}\n", _world_rank);
            return _sent_tasks;
        }

        [[nodiscard]] double get_idle_time(int rank) const override {
            if (_synchronized && _world_rank == 0) {
                return _idle_time_per_process[rank];
            }
            spdlog::warn("get_idle_time() called before synchronization, returning local value for this rank: {}\n", _world_rank);
            return _idle_time;
        }

        [[nodiscard]] double get_elapsed_time() const override {
            if (_world_rank != 0) {
                spdlog::warn("get_elapsed_time() called from non-center node, value collected only from center node");
                return 0;
            }
            return utils::diff_time(_start_time, _end_time) - static_cast<double>(TIMEOUT_TIME);
        }

        [[nodiscard]] size_t get_total_requested_tasks() const override {
            if (_world_rank != 0) {
                spdlog::warn("get_total_requested_tasks() called from non-center node, value collected only from center node");
                return 0;
            }
            return _total_requests_number;
        }

    public:
        /**
         * enqueue a message which will be sent to the next assigned process message pushing
         * is only possible when the preceding message has been successfully pushed to
         * another process, to avoid enqueuing.
         *
         * @param message the message to be sent
         */
        void push(std::string &&message, int function_id) override {
            if (message.empty()) {
                spdlog::throw_spdlog_ex(fmt::format("rank {}, attempted to send an empty buffer to rank {} \n", _world_rank, _next_process));
            }

            _transmitting = true;
            _destination_rank = _next_process;
            utils::print_mpi_debug_comments("rank {} entered Scheduler::push(..) for the node {}\n", _world_rank, _destination_rank);
            utils::shift_left(_next_processes);

            std::pair<std::string, int> pair = std::make_pair<std::string, int>(std::forward<std::string &&>(message), std::forward<int>(function_id));
            std::shared_ptr<std::pair<std::string, int>> message_ptr = std::make_shared<std::pair<std::string, int>>(pair);

            if (!_tasks_queue.empty()) {
                spdlog::throw_spdlog_ex("ERROR: _tasks_queue is not empty !!!!\n");
            }

            _tasks_queue.push(message_ptr);
            close_transmission_channel();
        }

        bool try_open_transmission_channel() override {
            if (!_mutex.try_lock()) {
                return false;
            }
            //mutex acquired
            if (_transmitting.load()) { // check if transmission is in progress
                _mutex.unlock();
                return false;
            }

            if (_next_process > 0) { // check if there is another process in the list
                return true; // priority acquired "mutex is meant to be released in closeSendingChannel() "
            }

            _mutex.unlock();
            return false;
        }

        void close_transmission_channel() override {
            _mutex.unlock();
        }

    private:

        //<editor-fold desc="Center node utility methods">
        /**	each node has an array containing its children were it is going to send tasks,
               *   this method puts the rank of these nodes into the array in the order that they
               *   are supposed to help the parent
              */
        void broadcast_nodes_topology() {
            // send initial topology to each process
            for (int rank = 1; rank < _world_size; rank++) {
                std::vector<int> buffer_tmp;
                for (auto &child: _process_tree[rank]) {
                    buffer_tmp.push_back(child);
                    _process_state[child] = ASSIGNED_STATE;
                    _nodes_available--;
                }
                if (buffer_tmp.empty()) {
                    continue;
                }
                MPI_Ssend(buffer_tmp.data(), (int) buffer_tmp.size(), MPI_INT, rank, NEXT_PROCESS, _next_process_communicator);
            }
        }

        /**
        * Sends a seed buffer to the rank 1 (first worker) process.
        *
        * @param buffer The buffer containing the seed data to be sent.
        * @param count The number of elements in the buffer.
        */
        void send_seed(const char *buffer, const int count) {
            const int dest = 1;
            // global synchronisation **********************
            _nodes_available--;
            _process_state[dest] = RUNNING_STATE;
            // *********************************************

            int err = MPI_Ssend(buffer, count, MPI_CHAR, dest, 0, _world_communicator); // send buffer
            if (err == MPI_SUCCESS) {
                spdlog::info("Seed sent \n");
                return;
            }

            spdlog::error("buffer failed to send! \n");
        }

        /**
         * return false if a message not received, which is signal of termination
         *  all workers report (with a message) to a centre process when about to run a task or when becoming available
         *  if no message is received within a TIMEOUT window, then all processes will have finished
         *
         * @return true if a message is received or timeout occurs
         */
        [[nodiscard]]bool receiveOrTimeout(int buffer, int &ready, double wTime0, MPI_Status &status, MPI_Request &request) {
            long cycles = 0;
            while (true) {
                MPI_Test(&request, &ready, &status);
                // Check whether the underlying communication had already taken place
                double wTime1 = MPI_Wtime();
                while (!ready && (TIMEOUT_TIME > utils::diff_time(wTime0, wTime1))) {
                    MPI_Test(&request, &ready, &status);
                    cycles++;
                }

                if (ready == true) {
                    return false;
                }

                if (_nodes_running == 0) {
                    // Cancellation due to TIMEOUT
                    MPI_Cancel(&request);
                    MPI_Request_free(&request);
                    spdlog::info("rank {} : receiving TIMEOUT, buffer : {}, cycles : {}\n", _world_rank, buffer, cycles);
                    return true;
                }
            }
        }

        /**
         * received if and only if a worker receives a message from other worker but center
         * @param status
         */
        void process_running(const MPI_Status &status) {
            _process_state[status.MPI_SOURCE] = RUNNING_STATE; // node was assigned, now it's running
            _nodes_running++;
            utils::print_mpi_debug_comments("rank {} reported running, nRunning :{}\n", status.MPI_SOURCE, _nodes_running);

            if (_process_tree[status.MPI_SOURCE].isAssigned()) {
                _process_tree[status.MPI_SOURCE].release();
            }

            if (!_process_tree[status.MPI_SOURCE].hasNext()) { // checks if notifying node has a child to push to
                int nxt = get_any_available_process();
                if (nxt > 0) {
                    MPI_Send(&nxt, 1, MPI_INT, status.MPI_SOURCE, NEXT_PROCESS, _next_process_communicator);
                    _process_tree[status.MPI_SOURCE].addNext(nxt);
                    _process_state[nxt] = ASSIGNED_STATE;
                    _nodes_available--;
                }
            }
            _total_requests_number++;
        }

        void process_available(const MPI_Status &status) {
            _process_state[status.MPI_SOURCE] = AVAILABLE_STATE;
            _nodes_available++;
            _nodes_running--;

            if (_process_tree[status.MPI_SOURCE].isAssigned()) {
                _process_tree[status.MPI_SOURCE].release();
            }
            utils::print_mpi_debug_comments("rank {} reported available, nRunning :{}\n", status.MPI_SOURCE, _nodes_running);
            std::random_device rd;                                        // Will be used to obtain a seed for the random number engine
            std::mt19937 gen(rd());                                   // Standard mersenne_twister_engine seeded with rd()
            std::uniform_int_distribution<> distrib(0, _world_size); // uniform distribution [closed interval]
            int random_offset = distrib(gen);                          // random value in range

            for (int i_rank = 0; i_rank < _world_size; i_rank++) {
                int rank = (i_rank + random_offset) % _world_size; // this ensures that rank is always in range 0 <= rank < world_size

                if (rank <= 0) continue;
                if (_process_state[rank] != RUNNING_STATE) continue; // finds the first running node
                if (_process_tree[rank].hasNext()) continue; // checks if running node has a child to push to

                MPI_Send(&status.MPI_SOURCE, 1, MPI_INT, rank, NEXT_PROCESS, _next_process_communicator);

                _process_tree[rank].addNext(status.MPI_SOURCE);      // assigns returning node to the running node
                _process_state[status.MPI_SOURCE] = ASSIGNED_STATE; // it flags returning node as assigned
                _nodes_available--;                                      // assigned, not available any more
                utils::print_mpi_debug_comments("ASSIGNMENT:\trank {} <-- [{}]\n", rank, status.MPI_SOURCE);
                break; // breaks for-loop
            }
        }

        /**
         * If the program flow reaches this stage, it signifies that the nodes have either attained an improved
         * local reference value or are outdated. Consequently, it becomes necessary to broadcast this value to
         * ensure all nodes are updated, regardless of whether it has changed or not.
         *
         * @param new_global_reference_value
         * @param status
         */
        void maybe_broadcast_global_reference_value(int new_global_reference_value, MPI_Status &status) {
            utils::print_mpi_debug_comments("center received refValue {} from rank {}\n", new_global_reference_value, status.MPI_SOURCE);
            bool reference_value_updated = false;
            switch (_lookup_strategy) {
                case MAXIMISE: {
                    if (new_global_reference_value > _global_reference_value) {
                        _global_reference_value = new_global_reference_value;
                        reference_value_updated = true;
                        for (int rank = 1; rank < _world_size; rank++) {
                            MPI_Send(&_global_reference_value, 1, MPI_INT, rank, REFERENCE_VAL_UPDATE, _global_reference_value_communicator);
                        }
                    }
                    break;
                }
                case MINIMISE: {
                    if (new_global_reference_value < _global_reference_value) {
                        _global_reference_value = new_global_reference_value;
                        reference_value_updated = true;
                        for (int rank = 1; rank < _world_size; rank++) {
                            MPI_Send(&_global_reference_value, 1, MPI_INT, rank, REFERENCE_VAL_UPDATE, _global_reference_value_communicator);
                        }
                    }
                    break;
                }
                default:
                    spdlog::throw_spdlog_ex("Unrecognised lookup strategy");
            }

            if (reference_value_updated) {
                static int success = 0;
                success++;
                spdlog::info("SUCCESSFUL updates: {}, _global_reference_value succeeded to update to : {} by rank {}\n", success, _global_reference_value, status.MPI_SOURCE);
            } else {
                static int failures = 0;
                failures++;
                spdlog::info("FAILED updates: {}, _global_reference_value failed to update to : {} by rank {}\n", failures, _global_reference_value, status.MPI_SOURCE);
            }
        }

        /**
    * This function sends a termination signal to all processes in the world communicator.
    */
        void notify_termination() {
            for (int rank = 1; rank < _world_size; rank++) {
                int unused = 0;
                int count = sizeof(unused);
                MPI_Send(&unused, count, MPI_INT, rank, TERMINATION, _world_communicator); // send positive signal
            }
            MPI_Barrier(_world_communicator);
        }

        int get_any_available_process() {
            for (int rank = 1; rank < _world_size; rank++) {
                if (_process_state[rank] == AVAILABLE_STATE) {
                    return rank;
                }
            }
            return -1; // all nodes are running
        }

        /**
         * receive solution from other processes
         */
        void receive_solution() {
            for (int rank = 1; rank < _world_size; rank++) {

                MPI_Status status;
                int count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(rank, MPI_ANY_TAG, _world_communicator, &status);     // receives status before receiving the message
                MPI_Get_count(&status, MPI_CHAR, &count);                               // receives total number of datatype elements of the message
                //***************************************************************************************************

                std::unique_ptr<char[]> buffer = std::make_unique<char[]>(count);
                MPI_Recv(buffer.get(), count, MPI_CHAR, rank, MPI_ANY_TAG, _world_communicator, &status);

                utils::print_mpi_debug_comments("fetching result from rank {} \n", rank);

                Tags tag = static_cast<Tags>(status.MPI_TAG);
                switch (tag) {
                    case HAS_RESULT: {
                        std::string message(buffer.get(), count);
                        int reference_value;
                        MPI_Recv(&reference_value, 1, MPI_INT, rank, HAS_RESULT, _world_communicator, &status);

                        _best_results_map[rank] = std::make_pair(reference_value, message);
                        spdlog::info("solution received from rank {}, count : {}, referenceValue {} \n", rank, count, reference_value);
                        break;
                    }
                    case NO_RESULT: {
                        spdlog::info("solution NOT received from rank {}\n", rank);
                        break;
                    }
                    default:
                        break;
                }
            }
        }
        //</editor-fold>

        //<editor-fold desc="Worker Node utility methods">

        /**
         * Probes the MPI communicator for a new message while continuously monitoring the reference value and next available process.
         *
         * This function behaves as follows:
         *  <li>It checks the global reference value and next available process, and if either has changed, it skips processing any incoming messages. </li>
         *  <li>If the reference value or next available process remains unchanged, it probes the MPI communicator for a new message using `MPI_Iprobe`. </li>
         *  <li>If a message is received, its status is returned; otherwise, the function continues to monitor changes in the reference value and next available process.</li>
         *
         * @return The status of the probe operation if a message was received, or an empty status otherwise.
         */
        MPI_Status probe_new_message() {
            MPI_Status status;
            int isMessageReceived = 0; //logical
            while (!isMessageReceived) {
                if (probe_reference_value()) { // different communicator
                    continue; // center might update this value even if this process is idle
                }
                if (probe_next_process()) { // different communicator
                    continue; // center might update this value even if this process is idle
                }

                MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, _world_communicator, &isMessageReceived, &status); // for regular messages
                if (isMessageReceived) {
                    break;
                }
            }
            return status;
        }

        void process_message(NodeManager &nodeManager) {
            MPI_Status status;
            int count; // count to be received
            MPI_Get_count(&status, MPI_CHAR, &count); // receives total number of datatype elements of the message

            std::unique_ptr<char[]> message = std::make_unique<char[]>(count);
            MPI_Recv(message.get(), count, MPI_CHAR, MPI_ANY_SOURCE, FUNCTION_ARGS, _world_communicator, &status);

            //receives function id
            int function_id;
            MPI_Recv(&function_id, 1, MPI_INT, MPI_ANY_SOURCE, FUNCTION_ID, _world_communicator, &status);

            notify_running_state_to_center();
            _received_tasks++;

            //  Delegates the processing of the incoming buffer to the branch handler and stores the potential returned value in _returned_value_future.
            const std::string args = std::string(message.get(), count);
            std::optional<std::shared_future<std::string>> returned_value_opt = nodeManager.forwardToExecutor(args, function_id);
            utils::print_mpi_debug_comments("rank {}, pushed buffer to thread pool \n", _world_rank, status.MPI_SOURCE);
            // **********************************************************************************************

            if (returned_value_opt.has_value()) {
                _returned_value_future = returned_value_opt.value();
            }
            task_funneling(nodeManager);
            notify_available_state_to_center();
        }

        void process_termination() const {
            MPI_Status status;
            int count; // optional? because we already know, it is 1
            MPI_Get_count(&status, MPI_INT, &count);

            int unused;
            MPI_Recv(&unused, count, MPI_INT, CENTER_NODE, TERMINATION, _world_communicator, &status);

            spdlog::info("rank {} exited\n", _world_rank);
            MPI_Barrier(_world_communicator);
        }

        void notify_running_state_to_center() {
            int unused = 0;
            MPI_Send(&unused, 1, MPI_INT, CENTER_NODE, RUNNING_STATE, _world_communicator);
        }

        //<editor-fold desc="Task Funneling">

        /**
         * Loops in here until the process run out of tasks
         * @param nodeManager
         */
        void task_funneling(NodeManager &nodeManager) {
            std::shared_ptr<std::pair<std::string, int>> task;
            bool isPop = _tasks_queue.pop(task);

            while (true) {
                while (isPop) { // as long as there is a message
                    std::scoped_lock<std::mutex> lck(_mutex);
                    _sent_tasks++;

                    send_task(task);
                    isPop = _tasks_queue.pop(task);

                    if (!isPop) {
                        _transmitting = false;
                    } else {
                        spdlog::throw_spdlog_ex("Task found in queue, this should not happen in task_funneling()\n");
                    }
                }

                {
                    /* this section protects MPI calls */
                    std::scoped_lock<std::mutex> lck(_mutex);
                    probe_reference_value();
                    probe_next_process();

                    maybe_send_result_back_to_non_void_source_rank();

                    maybe_update_reference_value(nodeManager);
                    update_local_next_process();
                }

                isPop = _tasks_queue.pop(task);

                if (!isPop && nodeManager.isDone()) {
                    /* by the time this thread realizes that the thread pool has no more tasks,
                        another buffer might have been pushed, which should be verified in the next line*/
                    isPop = _tasks_queue.pop(task);

                    if (!isPop) {
                        break;
                    }
                }
            }
            utils::print_mpi_debug_comments("rank {} sent {} tasks\n", _world_rank, _sent_tasks);

            if (!_tasks_queue.empty()) {
                spdlog::throw_spdlog_ex("leaving process with a pending message\n");
            }
            /* to reuse the task funneling, otherwise it will exit
            right away the second time the process receives a task*/
        }

        void send_task(const std::shared_ptr<std::pair<std::string, int>> &task) {
            const std::string message = task->first;
            const int function_id = task->second;

            if (message.size() > std::numeric_limits<int>::max()) {
                spdlog::throw_spdlog_ex("message is to long to be sent in a single message, currently not supported");
            }

            if (_destination_rank > 0) {
                if (_destination_rank == _world_rank) {
                    spdlog::throw_spdlog_ex("rank " + std::to_string(_world_rank) + " attempting to send to itself !!!\n");
                }

                utils::print_mpi_debug_comments("rank {} about to send buffer to rank {}\n", _world_rank, _destination_rank);
                MPI_Send(message.data(), static_cast<int>(message.size()), MPI_CHAR, _destination_rank, FUNCTION_ARGS, _world_communicator);
                utils::print_mpi_debug_comments("rank {} sent buffer to rank {}\n", _world_rank, _destination_rank);

                MPI_Send(&function_id, 1, MPI_INT, _destination_rank, FUNCTION_ID, _world_communicator);

                /**TODO ... future support!!
                 *  - Send a flag alerting that this is a VOID or NON_VOID function, so a result is expected.
                 *  - if so, no other task should be sent until the result is received
                 */

                _destination_rank = -1;
            } else {
                spdlog::throw_spdlog_ex("rank " + std::to_string(_world_rank) + ", could not send task to rank " + std::to_string(_destination_rank) + "\n");
            }
        }

        /**
         * Checks for a reference value update from the center node.
         * @return true if a reference value update was received, false otherwise.
         */
        int probe_reference_value() {
            int isMessageReceived = 0; // logical
            MPI_Status status;
            MPI_Iprobe(CENTER_NODE, REFERENCE_VAL_UPDATE, _global_reference_value_communicator, &isMessageReceived, &status);

            if (isMessageReceived) {
                utils::print_mpi_debug_comments("rank {}, about to receive refValue from Center\n", _world_rank);
                MPI_Recv(&_global_reference_value, 1, MPI_INT, CENTER_NODE, REFERENCE_VAL_UPDATE, _global_reference_value_communicator, &status);
                utils::print_mpi_debug_comments("rank {}, received refValue: {} from Center\n", _world_rank, _global_reference_value);

                return true;
            }
            return false;
        }

        /**
         * Checks for a new assigned process from center.
         * @return true if a new assigned process was received, false otherwise.
         */
        int probe_next_process() {
            int isMessageReceived = 0; // logical
            MPI_Status status;
            MPI_Iprobe(CENTER_NODE, NEXT_PROCESS, _next_process_communicator, &isMessageReceived, &status);

            if (isMessageReceived) {
                int count;
                MPI_Get_count(&status, MPI_INT, &count); // 0 < count < world_size   -- safe
                //TODO .. should throw if count is not in range

                utils::print_mpi_debug_comments("rank {}, about to receive nextProcess from Center, count : {}\n", _world_rank, count);
                MPI_Recv(_next_processes.data(), count, MPI_INT, CENTER_NODE, NEXT_PROCESS, _next_process_communicator, &status);
                utils::print_mpi_debug_comments("rank {}, received nextProcess from Center, count : {}\n", _world_rank, count);

                return true;
            }
            return false;
        }

        void maybe_send_result_back_to_non_void_source_rank() {
            if (!gempba::is_future_ready(_returned_value_future)) {
                return;
            }
            spdlog::throw_spdlog_ex("Non-void result not supported yet");
            const std::string &message = _returned_value_future.get();
            /** TODO... future support!
             *   - this should be blocking, sanity, as the _source_rank should be the rank that sent the task, and it is waiting for the result
             */

            MPI_Ssend(message.data(), static_cast<int>(message.size()), MPI_CHAR, _non_void_source_rank, NON_VOID_RESULT, _world_communicator);

            // _returned_value_future // should be reset here ?
            _non_void_source_rank = -1;
        }

        void maybe_update_reference_value(NodeManager &nodeManager) {
            const int _local_reference_value = nodeManager.refValue();

            if (should_update_local(_lookup_strategy, _global_reference_value, _local_reference_value)) {
                nodeManager.updateRefValue(_global_reference_value);
            } else if (should_update_global(_lookup_strategy, _global_reference_value, _local_reference_value)) {
                MPI_Ssend(&_local_reference_value, 1, MPI_INT, CENTER_NODE, REFERENCE_VAL_UPDATE, _world_communicator);
            }
        }

        static bool should_update_global(LookupStrategy strategy, int global_reference_value, int local_reference_value) {
            bool local_max_is_better = strategy == MAXIMISE && local_reference_value > global_reference_value;
            bool local_min_is_better = strategy == MINIMISE && local_reference_value < global_reference_value;
            return local_max_is_better || local_min_is_better;
        }

        static bool should_update_local(LookupStrategy strategy, int global_reference_value, int local_reference_value) {
            bool global_max_is_better = strategy == MAXIMISE && global_reference_value > local_reference_value;
            bool global_min_is_better = strategy == MINIMISE && global_reference_value < local_reference_value;
            return global_max_is_better || global_min_is_better;
        }

        /**
         * it separates receiving buffer from the local next process value
         */
        void update_local_next_process() {
            _next_process = _next_processes[0];
        }
        //</editor-fold>

        void notify_available_state_to_center() {
            int unused = 0;
            MPI_Send(&unused, 1, MPI_INT, CENTER_NODE, AVAILABLE_STATE, _world_communicator);
            utils::print_mpi_debug_comments("rank {} entered notify_available_state_to_center()\n", _world_rank);
        }

        void send_final_solution_to_center(NodeManager &nodeManager) {
            std::optional<std::pair<int, std::string>> best_solution_opt = nodeManager.getSerializedBestSolution();

            if (best_solution_opt.has_value()) {
                int reference_value = best_solution_opt.value().first;
                std::string message = best_solution_opt.value().second;
                MPI_Send(message.data(), static_cast<int>(message.size()), MPI_CHAR, CENTER_NODE, HAS_RESULT, _world_communicator);
                MPI_Send(&reference_value, 1, MPI_INT, CENTER_NODE, HAS_RESULT, _world_communicator);
            } else {
                char unused = 0;
                MPI_Send(&unused, 1, MPI_CHAR, CENTER_NODE, NO_RESULT, _world_communicator);
            }
        }
        //</editor-fold>

        static void check_MPI_error(std::string msg, int code1) {
            if (code1 != MPI_SUCCESS) {
                spdlog::throw_spdlog_ex(msg + ", with error code: " + std::to_string(code1));
            }
        }

    private:
        void init_mpi() {
            set_thread_support_level();
            create_mpi_communicators();

            int name_length;
            MPI_Get_processor_name(processor_name, &name_length);
            spdlog::info("Process {} of {} is on {}\n", _world_rank, _world_size, processor_name);
        }

        static void set_thread_support_level() {
            // Initialize MPI and ask for thread support
            int *argc = nullptr;
            char **argv = nullptr;
            int provided;
            MPI_Init_thread(argc, &argv, MPI_THREAD_FUNNELED, &provided);

            if (provided < MPI_THREAD_FUNNELED) {
                spdlog::info("The threading support level is lesser than that demanded.\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }
        }

        void create_mpi_communicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &_world_communicator);                  // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD, &_global_reference_value_communicator); // exclusive communicator for reference value - one-sided comm
            MPI_Comm_dup(MPI_COMM_WORLD, &_next_process_communicator);           // exclusive communicator for next process - one-sided comm

            MPI_Comm_size(_world_communicator, &_world_size);
            MPI_Comm_rank(_world_communicator, &_world_rank);
            MPI_Barrier(_world_communicator);

//            if (_world_size < 2) {
//                spdlog::info("At least two processes required !!\n");
//                MPI_Abort(_world_communicator, EXIT_FAILURE);
//            }
        }

        void init_member_variables() {
            _process_state.resize(_world_size, AVAILABLE_STATE);
            _process_tree.resize(_world_size);
            _next_processes.resize(_world_size, -1);
            _global_reference_value = INT_MIN;
            _transmitting = false;
        }

        void deallocateMPI() {
            MPI_Comm_free(&_global_reference_value_communicator);
            MPI_Comm_free(&_next_process_communicator);
            MPI_Comm_free(&_world_communicator);
            MPI_Finalize();
        }
    };
}

#endif //GEMPBA_MPISCHEDULER_H
