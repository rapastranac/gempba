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
#include <cstdio>
#include <cstdlib> /* srand, rand */
#include <fstream>
#include <memory>
#include <mpi.h>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

#include <utils/ipc/result.hpp>
#include <utils/ipc/task_packet.hpp>

#include "utils/gempba_utils.hpp"

#define TIMEOUT_TIME 3

// sanity assignment
enum tags {
    CENTER_NODE = 0,
    RUNNING_STATE = 1,
    ASSIGNED_STATE = 2,
    AVAILABLE_STATE = 3,
    TERMINATION = 4,
    REFERENCE_VAL_PROPOSAL = 5,
    REFERENCE_VAL_UPDATE = 6,
    NEXT_PROCESS = 7,
    HAS_RESULT = 8,
    NO_RESULT = 9,
    FUNCTION_ARGS = 10,
};

namespace gempba {
    class branch_handler;

    class ResultHolderParent;

    // inter process communication handler
    class mpi_scheduler final : public scheduler_parent {

    public:
        ~mpi_scheduler() override {
            finalize();
        }

        static mpi_scheduler &get_instance() {
            static mpi_scheduler instance;
            return instance;
        }

        [[nodiscard]] int rank_me() const override {
            return m_world_rank;
        }

        [[nodiscard]] size_t get_total_requests() const override {
            return m_total_requests_number;
        }

        void set_custom_initial_topology(tree &&p_tree) override {
            m_process_tree = std::move(p_tree);
            m_custom_initial_topology = true;
        }

        task_packet fetch_solution() override {
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                if (m_best_results[v_rank].get_reference_value() == m_global_reference_value) {
                    return m_best_results[v_rank].get_task_packet();
                }
            }
            return task_packet::EMPTY;
        }

        std::vector<result> fetch_result_vector() override {
            return m_best_results;
        }

        void print_stats() override {
            spdlog::debug("\n \n \n");
            spdlog::debug("*****************************************************\n");
            spdlog::debug("Elapsed time : {:4.3f} \n", elapsed_time());
            spdlog::debug("Total number of requests : {} \n", m_total_requests_number);
            spdlog::debug("*****************************************************\n");
            spdlog::debug("\n \n \n");
        }

        [[nodiscard]] double elapsed_time() const override {
            return (m_end_time - m_start_time) - static_cast<double>(TIMEOUT_TIME);
        }

        void allgather(void *p_recvbuf, void *p_sendbuf, MPI_Datatype p_mpi_datatype) override {
            MPI_Allgather(p_sendbuf, 1, p_mpi_datatype, p_recvbuf, 1, p_mpi_datatype, m_world_communicator);
            MPI_Barrier(m_world_communicator);
        }

        void gather(void *p_sendbuf, int p_sendcount, MPI_Datatype p_sendtype, void *p_recvbuf, int p_recvcount, MPI_Datatype p_recvtype, int p_root) override {
            MPI_Gather(p_sendbuf, p_sendcount, p_sendtype, p_recvbuf, p_recvcount, p_recvtype, p_root, m_world_communicator);
        }

        [[nodiscard]] int get_world_size() const override {
            return m_world_size;
        }

        [[nodiscard]] int tasks_recvd() const override {
            return m_received_tasks;
        }

        [[nodiscard]] int tasks_sent() const override {
            return m_sent_tasks;
        }

        void barrier() override {
            if (m_world_communicator != MPI_COMM_NULL)
                MPI_Barrier(m_world_communicator);
        }

        bool open_sending_channel() override {
            if (m_mutex.try_lock()) // acquires mutex
            {
                if (!m_transmitting.load()) // check if transmission in progress
                {
                    const int v_next = next_process();
                    if (v_next > 0) // check if there is another process in the list
                    {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                m_mutex.unlock();
            }
            return false;
        }

        /* this should be invoked only if priority is acquired*/
        void close_sending_channel() override {
            m_mutex.unlock();
        }

        // returns the process rank assigned to receive a task from this process
        [[nodiscard]] int next_process() const override {
            return this->m_next_process;
        }

        void set_goal(goal p_goal) override {
            this->m_goal = p_goal;

            if (p_goal == MINIMISE) {
                // minimisation
                m_global_reference_value = INT_MAX;
            }
        }

    private:
        MPI_Status probe_communicators() {
            static int branch = 0;

            while (true) {
                switch (branch % 3) {
                    case 0:
                        if (auto v = probe_reference_value_comm(); v.has_value()) {
                            return v.value();
                        }
                        break;
                    case 1:
                        if (auto v = probe_next_process_comm(); v.has_value()) {
                            return v.value();
                        }
                        break;
                    case 2:
                        if (auto v = probe_world_comm(); v.has_value()) {
                            return v.value();
                        }
                        break;
                }

                // Increment and reset before overflow
                if (++branch > INT_MAX - 1000) {
                    branch = 0;
                }
            }
        }

        void process_message(MPI_Status p_status, branch_handler &p_branch_handler, const std::function<std::shared_ptr<ResultHolderParent>(task_packet)> &p_buffer_decoder) {
            int v_count; // count to be received
            MPI_Get_count(&p_status, MPI_BYTE, &v_count); // receives total number of datatype elements of the message

            utils::print_mpi_debug_comments("rank {}, received message from rank {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, v_count);
            task_packet v_task_packet(v_count);
            MPI_Recv(v_task_packet.data(), v_count, MPI_BYTE, p_status.MPI_SOURCE, FUNCTION_ARGS, m_world_communicator, &p_status);

            notify_running_state();
            m_received_tasks++;

            //  push to the thread pool *********************************************************************
            std::shared_ptr<ResultHolderParent> v_holder = p_buffer_decoder(v_task_packet); // holder might be useful for non-void functions
            utils::print_mpi_debug_comments("rank {}, pushed buffer to thread pool \n", m_world_rank, p_status.MPI_SOURCE);
            // **********************************************************************************************

            task_funneling(p_branch_handler);
            notify_available_state();
        }

        void process_termination(MPI_Status p_status) {
            constexpr int v_count = 1; // count to be received
            int v_unused;
            MPI_Recv(&v_unused, v_count, MPI_INT, p_status.MPI_SOURCE, TERMINATION, m_world_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received message from rank {}", m_world_rank, p_status.MPI_SOURCE);

            spdlog::debug("rank {} exited\n", m_world_rank);
            MPI_Barrier(m_world_communicator);
        }

    public:
        void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<ResultHolderParent>(task_packet)> &p_buffer_decoder,
                      std::function<result()> &p_result_fetcher) override {
            MPI_Barrier(m_world_communicator);

            bool v_is_terminated = false;
            while (true) {
                MPI_Status v_status = probe_communicators();

                switch (v_status.MPI_TAG) {
                    case TERMINATION: {
                        process_termination(v_status);
                        v_is_terminated = true; // temporary, it should always happen
                        break;
                    }
                    case REFERENCE_VAL_UPDATE: {
                        receive_reference_value(v_status);
                        break;
                    }
                    case NEXT_PROCESS: {
                        receive_next_process(v_status);
                        break;
                    }
                    case FUNCTION_ARGS: {
                        process_message(v_status, p_branch_handler, p_buffer_decoder);
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

            send_solution(p_result_fetcher);
        }

        /* enqueue a message which will be sent to the next assigned process
            message pushing is only possible when the preceding message has been successfully pushed
            to another process, to avoid enqueuing.
        */
        void push(task_packet &&p_task_packet) override {
            if (p_task_packet.empty()) {
                throw std::runtime_error(fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank));
            }

            m_transmitting = true;
            m_destination_rank = next_process();
            utils::print_mpi_debug_comments("rank {} entered MPI_Scheduler::push(..) for the node {}\n", m_world_rank, m_destination_rank);
            utils::shift_left(m_next_processes);

            const auto v_pck = std::make_shared<task_packet>(std::forward<task_packet &&>(p_task_packet));
            const auto v_message = new task_packet(*v_pck);

            if (!m_tasks_queue.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            m_tasks_queue.push(v_message);
            close_sending_channel();
        }

    private:
        void task_funneling(branch_handler &p_branch_handler);

        void receive_reference_value(MPI_Status p_status) {
            utils::print_mpi_debug_comments("rank {}, about to receive refValue from Center\n", m_world_rank);
            MPI_Recv(&m_global_reference_value, 1, MPI_INT, CENTER_NODE, REFERENCE_VAL_UPDATE, m_global_reference_value_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received refValue: {} from Center\n", m_world_rank, m_global_reference_value);
        }

        std::optional<MPI_Status> probe_reference_value_comm() {
            int v_is_message_received = 0; // logical

            MPI_Status v_status;
            MPI_Iprobe(CENTER_NODE, REFERENCE_VAL_UPDATE, m_global_reference_value_communicator, &v_is_message_received, &v_status);
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
            MPI_Recv(m_next_processes.data(), v_count, MPI_INT, CENTER_NODE, NEXT_PROCESS, m_next_process_communicator, &p_status);
            utils::print_mpi_debug_comments("rank {}, received nextProcess from Center, count : {}\n", m_world_rank, v_count);
        }

        std::optional<MPI_Status> probe_next_process_comm() {
            int v_is_message_received = 0;

            MPI_Status v_status;
            MPI_Iprobe(CENTER_NODE, NEXT_PROCESS, m_next_process_communicator, &v_is_message_received, &v_status);
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
        void update_ref_value(branch_handler &p_branch_handler);

        /*	- return true is priority is acquired, false otherwise
            - priority released automatically if a message is pushed, otherwise it should be released manually
            - only ONE buffer will be enqueued at a time
            - if the taskFunneling is transmitting the buffer to another node, this method will return false
            - if previous conditions are met, then the actual condition for pushing is evaluated next_process[0] > 0
        */

        // it separates receiving buffer from the local
        void update_next_process() {
            m_next_process = m_next_processes[0];
        }

        void notify_available_state() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, CENTER_NODE, AVAILABLE_STATE, m_world_communicator);
            utils::print_mpi_debug_comments("rank {} entered notifyAvailableState()\n", m_world_rank);
        }

        void notify_running_state() {
            int buffer = 0;
            MPI_Send(&buffer, 1, MPI_INT, CENTER_NODE, RUNNING_STATE, m_world_communicator);
        }

        void send_task(task_packet p_task_packet) {
            const size_t v_message_length = p_task_packet.size();
            if (v_message_length > std::numeric_limits<int>::max()) {
                throw std::runtime_error("message is to long to be sent in a single message, currently not supported");
            }

            if (m_destination_rank > 0) {
                if (m_destination_rank == m_world_rank) {
                    throw std::runtime_error("rank " + std::to_string(m_world_rank) + " attempting to send to itself !!!\n");
                }
                utils::print_mpi_debug_comments("rank {} about to send buffer to rank {}\n", m_world_rank, m_destination_rank);
                MPI_Send(p_task_packet.data(), static_cast<int>(v_message_length), MPI_BYTE, m_destination_rank, FUNCTION_ARGS, m_world_communicator);
                utils::print_mpi_debug_comments("rank {} sent buffer to rank {}\n", m_world_rank, m_destination_rank);
                m_destination_rank = -1;
            } else {
                throw std::runtime_error("rank " + std::to_string(m_world_rank) + ", could not send task to rank " + std::to_string(m_destination_rank) + "\n");
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
                for (auto &child: m_process_tree[rank]) {
                    buffer_tmp.push_back(child);

                    m_process_state[child] = ASSIGNED_STATE;
                    --m_nodes_available;
                }
                if (!buffer_tmp.empty()) {
                    MPI_Ssend(buffer_tmp.data(), (int) buffer_tmp.size(), MPI_INT, rank, NEXT_PROCESS, m_next_process_communicator);
                }
            }
        }

        // already adapted for multibranching
        static int get_next_process(int j, int pi, int b, int depth) {
            return (j * pow(b, depth)) + pi;
        }

        /*	send a solution achieved from node to the center node */
        void send_solution(const std::function<result()> &p_result_fetcher) {
            const result v_result = p_result_fetcher();
            const int v_ref_val = v_result.get_reference_value();;
            task_packet v_task_packet = v_result.get_task_packet();

            if (v_result == result::EMPTY) {
                MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            } else {
                MPI_Send(v_task_packet.data(), static_cast<int>(v_task_packet.size()), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
                MPI_Send(&v_ref_val, 1, MPI_INT, CENTER_NODE, HAS_RESULT, m_world_communicator);
            }
        }

        /* it returns the substraction between end and start*/
        static double difftime(const double p_start, const double p_end) {
            return p_end - p_start;
        }

        static int consume_flag(MPI_Status p_status, const MPI_Comm &p_communicator) {
            int v_val = 0;;
            MPI_Recv(&v_val, 1, MPI_INT, p_status.MPI_SOURCE, p_status.MPI_TAG, p_communicator, &p_status);
            return v_val;
        }

        void process_running(MPI_Status p_status) {
            m_process_state[p_status.MPI_SOURCE] = RUNNING_STATE; // node was assigned, now it's running
            ++m_nodes_running;
            utils::print_mpi_debug_comments("rank {} reported running, nRunning :{}\n", p_status.MPI_SOURCE, m_nodes_running);

            if (m_process_tree[p_status.MPI_SOURCE].is_assigned())
                m_process_tree[p_status.MPI_SOURCE].release();

            if (!m_process_tree[p_status.MPI_SOURCE].has_next()) // checks if notifying node has a child to push to
            {
                const int next = get_available();
                if (next > 0) {
                    // put(&nxt, 1, status.MPI_SOURCE, MPI_INT, 0, win_nextProcess);
                    MPI_Send(&next, 1, MPI_INT, p_status.MPI_SOURCE, NEXT_PROCESS, m_next_process_communicator);
                    m_process_tree[p_status.MPI_SOURCE].add_next(next);
                    m_process_state[next] = ASSIGNED_STATE;
                    --m_nodes_available;
                }
            }
            m_total_requests_number++;
        }

        void process_available(MPI_Status p_status) {
            m_process_state[p_status.MPI_SOURCE] = AVAILABLE_STATE;
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
                if (m_process_state[v_rank] != RUNNING_STATE) {
                    continue;
                }

                // checks if running node has a child to push to
                if (m_process_tree[v_rank].has_next()) {
                    continue;
                }

                MPI_Send(&p_status.MPI_SOURCE, 1, MPI_INT, v_rank, NEXT_PROCESS, m_next_process_communicator);

                m_process_tree[v_rank].add_next(p_status.MPI_SOURCE); // assigns the returning node as a running node
                m_process_state[p_status.MPI_SOURCE] = ASSIGNED_STATE; // it flags the returning node as assigned
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

            const bool v_should_broadcast = should_broadcast_global(m_goal, m_global_reference_value, p_new_global_reference_value);
            if (!v_should_broadcast) {
                static int failures = 0;
                failures++;
                spdlog::debug("FAILED updates : {}, refValueGlobal : {} by rank {}\n", failures, m_global_reference_value, p_status.MPI_SOURCE);
                return;
            }

            m_global_reference_value = p_new_global_reference_value;
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                MPI_Send(&m_global_reference_value, 1, MPI_INT, v_rank, REFERENCE_VAL_UPDATE, m_global_reference_value_communicator);
            }

            static int success = 0;
            success++;
            spdlog::info("SUCCESSFUL updates: {}, refValueGlobal updated to : {} by rank {}\n", success, m_global_reference_value, p_status.MPI_SOURCE);
        }

        std::optional<MPI_Status> probe_reference_value_comm_center() {
            int v_is_message_received = 0; // logical
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, REFERENCE_VAL_PROPOSAL, m_global_reference_value_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                // spdlog::info("rank {}: probe_reference_value_comm_center received message from rank {}\n", world_rank, status.MPI_SOURCE);
                return v_status;
            }
            return std::nullopt;
        }

        std::optional<MPI_Status> probe_world_comm_center() {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        /**
         * Only returns when a message is received from any of the communicators.
         * @return MPI_Status of the received message, or std::nullopt only when all processes have finished or a timeout occurs.
         */
        std::optional<MPI_Status> probe_communicators_center() {
            long v_cycles = 0;
            static int branch = 0;
            while (true) {
                const double v_wall_time0 = MPI_Wtime();
                while (true) {
                    switch (branch % 2) {
                        case 0: {
                            if (const auto v_optional = probe_world_comm_center(); v_optional.has_value()) {
                                return v_optional;
                            }
                            break;
                        }
                        case 1: {
                            if (auto v_optional = probe_reference_value_comm_center(); v_optional.has_value()) {
                                return v_optional;
                            }
                            break;
                        }
                    }
                    if (++branch > INT_MAX - 1000) {
                        branch = 0;
                    }
                    ++v_cycles;
                    double v_elapsed = difftime(v_wall_time0, MPI_Wtime());
                    if (v_elapsed > TIMEOUT_TIME) {
                        spdlog::debug("rank {}: no messages received in {} seconds, cycles: {}", m_world_rank, v_elapsed, v_cycles);
                        break;
                    }
                }

                if (m_nodes_running < 0) {
                    spdlog::throw_spdlog_ex("rank {}: m_nodes_running is negative, this should not happen", m_world_rank);
                }

                // Timeout or termination condition
                if (m_nodes_running == 0) {
                    spdlog::debug("rank {}: probe_communicators_center received TIMEOUT", m_world_rank);
                    return std::nullopt;
                }
            }
        }

    public:
        /*	run the center node */
        void run_center(task_packet &p_seed) override {
            task_packet v_task_packet = p_seed;
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();

            if (!m_custom_initial_topology) {
                utils::build_topology(m_process_tree, 1, 0, 2, m_world_size);
            }
            broadcast_nodes_topology();
            send_seed(v_task_packet);

            while (true) {
                auto v_status_opt = probe_communicators_center();
                if (!v_status_opt.has_value()) {
                    spdlog::info("rank {}: probe_communicators_center received TIMEOUT, exiting center run", m_world_rank);
                    break;
                }
                MPI_Status v_status = v_status_opt.value();

                switch (v_status.MPI_TAG) {
                    case RUNNING_STATE: {
                        // received if and only if a worker receives from other but center
                        consume_flag(v_status, m_world_communicator);
                        process_running(v_status);
                        break;
                    }
                    case AVAILABLE_STATE: {
                        consume_flag(v_status, m_world_communicator);
                        process_available(v_status);
                        break;
                    }
                    case REFERENCE_VAL_PROPOSAL: {
                        int v_candidate_global_reference_value = consume_flag(v_status, m_global_reference_value_communicator);
                        maybe_broadcast_global_reference_value(v_candidate_global_reference_value, v_status);
                        break;
                    }
                }
            }

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notify_termination();

            // receive solution from other processes
            receive_solution();

            m_end_time = MPI_Wtime();
        }

    private:
        /* return false if message not received, which is signal of termination
            all workers report (with a message) to center process when about to run a task or when becoming available
            if no message is received within a TIMEOUT window, then all processes will have finished
        */
        bool receive_or_timeout(int p_buffer, int &p_ready, double p_wall_time0, MPI_Status &p_status, MPI_Request &p_request) {
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

        void notify_termination() {
            for (int rank = 1; rank < m_world_size; rank++) {
                constexpr int v_buffer = 0;
                constexpr int v_count = 1;
                MPI_Send(&v_buffer, v_count, MPI_INT, rank, TERMINATION, m_world_communicator); // send positive signal
            }
            MPI_Barrier(m_world_communicator);
        }

        int get_available() {
            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_process_state[rank] == AVAILABLE_STATE)
                    return rank;
            }
            return -1; // all nodes are running
        }

        /*	receive solution from nodes */
        void receive_solution() {
            for (int rank = 1; rank < m_world_size; rank++) {

                MPI_Status status;
                int count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(rank, MPI_ANY_TAG, m_world_communicator, &status); // receives status before receiving the message
                MPI_Get_count(&status, MPI_BYTE, &count); // receives total number of datatype elements of the message
                //***************************************************************************************************

                task_packet v_task_packet(count);
                MPI_Recv(v_task_packet.data(), count, MPI_BYTE, rank, MPI_ANY_TAG, m_world_communicator, &status);

                utils::print_mpi_debug_comments("fetching result from rank {} \n", rank);

                switch (status.MPI_TAG) {
                    case HAS_RESULT: {
                        int v_ref_value;
                        MPI_Recv(&v_ref_value, 1, MPI_INT, rank, HAS_RESULT, m_world_communicator, &status);

                        m_best_results[rank] = result{v_ref_value, v_task_packet};
                        spdlog::debug("solution received from rank {}, count : {}, refVal {} \n", rank, count, v_ref_value);
                        break;
                    }
                    case NO_RESULT: {
                        spdlog::debug("solution NOT received from rank {}\n", rank);
                        break;
                    }
                }
            }
        }

        void send_seed(task_packet &p_packet) {
            const int dest = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[dest] = RUNNING_STATE;
            // *********************************************

            int err = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, dest, FUNCTION_ARGS, m_world_communicator); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

        void create_communicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &m_world_communicator); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD, &m_global_reference_value_communicator); // exclusive communicator for reference value - one-sided comm
            MPI_Comm_dup(MPI_COMM_WORLD, &m_next_process_communicator); // exclusive communicator for next process - one-sided comm

            MPI_Comm_size(m_world_communicator, &this->m_world_size);
            MPI_Comm_rank(m_world_communicator, &this->m_world_rank);

            /*if (world_size < 2)
            {
                spdlog::debug("At least two processes required !!\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }*/
        }

        void allocate_mpi() {
            MPI_Barrier(m_world_communicator);
            init();
            MPI_Barrier(m_world_communicator);
        }

        void deallocate_mpi() {
            MPI_Comm_free(&m_global_reference_value_communicator);
            MPI_Comm_free(&m_next_process_communicator);
            MPI_Comm_free(&m_world_communicator);
        }

        void init() {
            m_process_state.resize(m_world_size, AVAILABLE_STATE);
            m_process_tree.resize(m_world_size);
            m_next_processes.resize(m_world_size, -1);
            m_global_reference_value = INT_MIN;

            if (m_world_rank == 0) {
                m_best_results.resize(m_world_size, result::EMPTY);
            }

            m_transmitting = false;
        }

    private:
        int m_argc;
        char **m_argv;
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

        Queue<task_packet *> m_tasks_queue;

        MPI_Comm m_global_reference_value_communicator; // BIDIRECTIONAL
        MPI_Comm m_next_process_communicator; // CENTER TO WORKER (ONLY)
        MPI_Comm m_world_communicator; // BIDIRECTIONAL

        std::vector<int> m_next_processes;
        int m_global_reference_value;

        int m_next_process = -1;

        goal m_goal = MAXIMISE;

        std::vector<result> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        // statistics
        size_t m_total_requests_number = 0;
        double m_start_time = 0;
        double m_end_time = 0;

        /* singleton*/
        mpi_scheduler() {
            init(NULL, NULL);
        }

        void init(int *p_argc, char *p_argv[]) {
            // Initialise MPI and ask for thread support
            int v_provided;
            MPI_Init_thread(p_argc, &p_argv, MPI_THREAD_FUNNELED, &v_provided);

            if (v_provided < MPI_THREAD_FUNNELED) {
                spdlog::debug("The threading support level is lesser than that demanded.\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }

            create_communicators();

            int v_namelen;
            MPI_Get_processor_name(m_processor_name, &v_namelen);
            spdlog::debug("Process {} of {} is on {}\n", m_world_rank, m_world_size, m_processor_name);
            allocate_mpi();
        }

        void finalize() {
            utils::print_mpi_debug_comments("rank {}, before deallocate \n", m_world_rank);
            deallocate_mpi();
            utils::print_mpi_debug_comments("rank {}, after deallocate \n", m_world_rank);
            MPI_Finalize();
            utils::print_mpi_debug_comments("rank {}, after MPI_Finalize() \n", m_world_rank);
        }

        /* ---------------------------------------------------------------------------------
         * utility functions to determine whether to update global or local reference values
         * ---------------------------------------------------------------------------------*/

        static bool should_update_global(const goal p_goal, const int p_global_reference_value, const int p_local_reference_value) {
            return should_update(p_goal, p_global_reference_value, p_local_reference_value);
        }

        static bool should_update_local(const goal p_goal, const int p_global_reference_value, const int p_local_reference_value) {
            return should_update(p_goal, p_local_reference_value, p_global_reference_value);
        }

        static bool should_broadcast_global(const goal p_goal, const int p_old_global, const int p_new_global) {
            return should_update(p_goal, p_old_global, p_new_global);
        }

        static bool should_update(const goal p_goal, const int p_old_value, const int p_new_value) {
            const bool new_max_is_better = p_goal == MAXIMISE && p_new_value > p_old_value;
            const bool new_min_is_better = p_goal == MINIMISE && p_new_value < p_old_value;
            return new_max_is_better || new_min_is_better;
        }
    };

}

#endif
