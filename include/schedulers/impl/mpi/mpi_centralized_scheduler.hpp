#ifndef GEMPBA_MPI_SCHEDULER_CENTRALIZED_HPP
#define GEMPBA_MPI_SCHEDULER_CENTRALIZED_HPP

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
#include <cstdlib> /* srand, rand */
#include <memory>
#include <mpi.h>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

#include <result_holder/result_holder_parent.hpp>
#include <schedulers/api/scheduler.hpp>
#include <schedulers/impl/mpi/centralized_utils.hpp>
#include <schedulers/impl/mpi/default_mpi_stats.hpp>
#include <spdlog/spdlog.h>
#include <utils/Queue.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/tree.hpp>
#include <utils/utils.hpp>
#include <utils/ipc/result.hpp>
#include <utils/ipc/task_packet.hpp>

// max memory is in mb, e.g. 1024 * 10 = 10 GB
#define MAX_MEMORY_MB (1024 * 10)
#define CENTER_NBSTORED_TASKS_PER_PROCESS 1000

namespace gempba {

    class branch_handler;

    // inter process communication handler
    class mpi_centralized_scheduler final : public scheduler {

        // sanity assignment
        enum tags {
            CENTER_NODE = 0,
            RUNNING_STATE = 1,
            ASSIGNED_STATE = 2,
            AVAILABLE_STATE = 3,
            TERMINATION = 4,
            SCORE_PROPOSAL = 5,
            SCORE_UPDATE = 6,
            NEXT_PROCESS = 7,
            HAS_RESULT = 8,
            NO_RESULT = 9,
            TASK_FROM_CENTER = 10,
            TASK_FOR_CENTER = 11,
            CENTER_IS_FULL = 12,
            CENTER_IS_FREE = 13,
        };

        std::priority_queue<task_packet, std::vector<task_packet>, TaskComparator> m_center_queue; //message, size

        int m_max_queue_size;
        bool m_center_last_full_status = false;
        double m_time_centerfull_sent = 0;

    public:
        ~mpi_centralized_scheduler() override {
            // MPI specific finalization
            finalize();
            spdlog::info("Goodbye from MPI Centralized Scheduler");
        }

        static mpi_centralized_scheduler &get_instance(const double p_timeout = 3.0) {
            static mpi_centralized_scheduler instance(p_timeout);
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
                if (m_best_results[v_rank].get_score() == m_global_score) {
                    return m_best_results[v_rank].get_task_packet();
                }
            }
            return task_packet::EMPTY; // no solution found
        }

        std::vector<result> fetch_result_vector() override {
            return m_best_results;
        }

        [[nodiscard]] std::unique_ptr<stats> get_stats() const override {
            return std::make_unique<default_mpi_stats>(m_stats);
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
            return (m_end_time - m_start_time) - static_cast<double>(m_timeout);
        }

        [[nodiscard]] int next_process() const override {
            return 0;
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
            if (m_world_communicator != MPI_COMM_NULL) {
                MPI_Barrier(m_world_communicator);
            }
        }

        bool try_open_transmission_channel() override {
            // acquires mutex
            if (m_mutex.try_lock()) {
                // check if transmission in progress
                if (!m_transmitting.load()) {
                    // check if center is actually waiting for a task
                    if (!m_is_center_full) {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                m_mutex.unlock();
            }
            return false;
        }

        /* this should be invoked only if channel is open*/
        void close_transmission_channel() override {
            m_mutex.unlock();
        }

        void set_goal(const goal p_goal, const score_type p_type) override {
            this->m_goal = p_goal;
            this->m_global_score = utils::get_default_score(p_goal, p_type);
        }


        void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                      std::function<result()> &p_result_fetcher) override {
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();

            bool v_is_terminated = false;
            while (true) {
                MPI_Status v_status = probe_communicators_at_worker();

                switch (v_status.MPI_TAG) {
                    case TERMINATION: {
                        process_termination(v_status);
                        collect_stats_data(p_branch_handler);
                        v_is_terminated = true; // temporary, it should always happen
                        break;
                    }
                    case SCORE_UPDATE: {
                        receive_score_from_center(v_status);
                        break;
                    }
                    case CENTER_IS_FULL: {
                        consume_center_is_full_tag(v_status);
                        break;
                    }
                    case CENTER_IS_FREE: {
                        consume_center_is_free_tag(v_status);
                        break;
                    }
                    case TASK_FROM_CENTER: {
                        process_message(v_status, p_branch_handler, p_buffer_decoder);
                        break;
                    }
                    default: {
                        // nothing
                    };
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
            m_end_time = MPI_Wtime();
            m_stats.m_elapsed_time = m_end_time - m_start_time;
        }

        /**
         * Forces pushing a task packet to the next assigned process. This method is not thread-safe.
         * @param p_task_packet The serialized message to be sent.
         */
        void push(task_packet &&p_task_packet) override {
            if (p_task_packet.empty()) {
                throw std::runtime_error(fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank));
            }

            m_transmitting = true;

            const auto v_pck = std::make_shared<task_packet>(std::forward<task_packet &&>(p_task_packet));
            const auto v_message = new task_packet(*v_pck);

            if (!m_tasks_queue.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            m_tasks_queue.push(v_message);

            close_transmission_channel();
        }

    private:
        MPI_Status probe_communicators_at_worker() {
            static int branch = 0;

            while (true) {
                switch (branch % 3) {
                    case 0:
                        if (auto v = probe_score_comm_at_node(); v.has_value()) {
                            return v.value();
                        }
                        break;
                    case 1:
                        if (auto v = probe_center_fullness_comm(); v.has_value()) {
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

        void process_termination(MPI_Status p_status) {

            MPI_Recv(nullptr, 0, MPI_BYTE, p_status.MPI_SOURCE, TERMINATION, m_world_communicator, &p_status);
            utils::print_ipc_debug_comments("rank {}, received message from rank {}", m_world_rank, p_status.MPI_SOURCE);

            spdlog::debug("rank {} exited\n", m_world_rank);
            MPI_Barrier(m_world_communicator);
        }

        void collect_stats_data(const branch_handler &p_branch_handler);

        void receive_score_from_center(MPI_Status p_status) {
            utils::print_ipc_debug_comments("rank {}, about to receive global score from Center\n", m_world_rank);
            MPI_Recv(&m_global_score, sizeof(score), MPI_BYTE, CENTER_NODE, SCORE_UPDATE, m_global_score_communicator, &p_status);
            utils::print_ipc_debug_comments("rank {}, received global score: {} from Center\n", m_world_rank, m_global_score.to_string());
        }

        void consume_center_is_full_tag(MPI_Status p_status) {
            MPI_Recv(nullptr, 0, MPI_BYTE, CENTER_NODE, CENTER_IS_FULL, m_center_fullness_communicator, &p_status);
            m_is_center_full = true;

            utils::print_ipc_debug_comments("Node {} received full center\n", rank_me());
        }

        void consume_center_is_free_tag(MPI_Status p_status) {
            MPI_Recv(nullptr, 0, MPI_BYTE, CENTER_NODE, CENTER_IS_FREE, m_center_fullness_communicator, &p_status);
            m_is_center_full = false;

            utils::print_ipc_debug_comments("Node {} received free center\n", rank_me());
        }

        void process_message(MPI_Status p_status, branch_handler &p_branch_handler, const std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder) {
            // Receives the task  -------------------------------------------------------------------------------------------
            int v_count; // count to be received
            MPI_Get_count(&p_status, MPI_BYTE, &v_count); // receives total number of datatype elements of the message

            utils::print_ipc_debug_comments("rank {}, received message from rank {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, v_count);
            task_packet v_task_packet(v_count);
            MPI_Recv(v_task_packet.data(), v_count, MPI_BYTE, p_status.MPI_SOURCE, p_status.MPI_TAG, m_world_communicator, &p_status);

            utils::print_ipc_debug_comments("rank {}, received message from rank {}, tag {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, p_status.MPI_TAG, v_count);

            // Here, we have a task to process  -----------------------------------------------------------------------------
            notify_running_state();
            m_received_tasks++;
            m_total_requests_number++;
            m_stats.m_received_task_count++;
            m_stats.m_total_requested_tasks++;

            utils::print_ipc_debug_comments("rank {}, pushing buffer to thread pool", m_world_rank, p_status.MPI_SOURCE);

            //  push to the thread pool *********************************************************************
            std::shared_ptr<result_holder_parent> v_holder = p_buffer_decoder(v_task_packet); // holder might be useful for non-void functions
            utils::print_ipc_debug_comments("rank {}, pushed buffer to thread pool \n", m_world_rank, p_status.MPI_SOURCE);
            // **********************************************************************************************

            task_funneling(p_branch_handler);
            notify_available_state();
        }

        // when a node is working, it loops through here
        void task_funneling(branch_handler &p_branch_handler);

        std::optional<MPI_Status> probe_score_comm_at_node() {
            int v_is_message_received = 0; // logical

            MPI_Status v_status;
            MPI_Iprobe(CENTER_NODE, SCORE_UPDATE, m_global_score_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                return v_status;

            }
            return std::nullopt;
        }

        // checks for a score update from center
        void maybe_receive_score_from_center() {
            const std::optional<MPI_Status> v_opt = probe_score_comm_at_node();
            if (v_opt.has_value()) {
                receive_score_from_center(v_opt.value());
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

        std::optional<MPI_Status> probe_center_fullness_comm() {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(CENTER_NODE, MPI_ANY_TAG, m_center_fullness_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        // checks for a new assigned process from center
        void maybe_receive_center_fullness() {
            std::optional<MPI_Status> opt = probe_center_fullness_comm();
            if (!opt.has_value()) {
                return;
            }
            const MPI_Status status = opt.value();

            if (status.MPI_TAG == CENTER_IS_FULL) {
                consume_center_is_full_tag(status);
            } else if (status.MPI_TAG == CENTER_IS_FREE) {
                consume_center_is_free_tag(status);
            }
        }

        // if score is received, it attempts updating local value
        // if local value is better than the one in center, then the local best value is sent to center
        void update_score(branch_handler &p_branch_handler);

        void notify_available_state() {
            utils::print_ipc_debug_comments("rank {} entered notify_available_state()\n", m_world_rank);
            constexpr int v_buffer = 0;
            MPI_Send(&v_buffer, 1, MPI_INT, CENTER_NODE, AVAILABLE_STATE, m_world_communicator);
        }

        void notify_running_state() {
            constexpr int v_buffer = 0;
            MPI_Send(&v_buffer, 1, MPI_INT, CENTER_NODE, RUNNING_STATE, m_world_communicator);
        }

        void send_task_to_center(task_packet &p_task_packet) {
            MPI_Send(p_task_packet.data(), static_cast<int>(p_task_packet.size()), MPI_BYTE, CENTER_NODE, TASK_FOR_CENTER, m_world_communicator);
        }

    public:
    private:
        /*	send solution attained from node to the center node */
        void send_solution(const std::function<result()> &p_result_fetcher) {
            const result v_result = p_result_fetcher();
            const score v_score = v_result.get_score();
            task_packet v_task_packet = v_result.get_task_packet();

            if (v_result == result::EMPTY) {
                MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            } else {
                MPI_Send(v_task_packet.data(), static_cast<int>(v_task_packet.size()), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
                MPI_Send(&v_score, sizeof(score), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
            }
        }

        static int consume_int_flag(MPI_Status p_status, const MPI_Comm &p_communicator) {
            int v_buffer;
            MPI_Recv(&v_buffer, 1, MPI_INT, p_status.MPI_SOURCE, p_status.MPI_TAG, p_communicator, &p_status);
            return v_buffer;
        }

        score consume_score_flag(MPI_Status p_status) {
            score v_score;
            MPI_Recv(&v_score, sizeof(score), MPI_BYTE, p_status.MPI_SOURCE, p_status.MPI_TAG, m_global_score_communicator, &p_status);
            return v_score;
        }

        void clear_buffer() {
            if (m_center_queue.empty())
                return;

            for (int rank = 1; rank < m_world_size; rank++) {
                if (m_process_state[rank] == AVAILABLE_STATE) {
                    //pair<char *, size_t> msg = center_queue.back();
                    //center_queue.pop_back();
                    task_packet v_msg = m_center_queue.top();
                    m_center_queue.pop();

                    MPI_Send(v_msg.data(), static_cast<int>(v_msg.size()), MPI_BYTE, rank, TASK_FROM_CENTER, m_world_communicator);
                    m_process_state[rank] = ASSIGNED_STATE;

                    if (m_center_queue.empty())
                        return;
                }
            }
        }

    public:
        /*	run the center node */
        void run_center(task_packet &p_seed) override {
            task_packet v_task_packet = p_seed;
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();

            send_seed(v_task_packet);

            while (true) {
                auto v_status_opt = probe_communicators_at_center();
                if (!v_status_opt.has_value()) {
                    spdlog::info("rank {}: probe_communicators_center received TIMEOUT, exiting center run", m_world_rank);
                    break;
                }
                MPI_Status v_status = v_status_opt.value();

                switch (v_status.MPI_TAG) {
                    case RUNNING_STATE: {
                        // received if and only if a worker receives from other but center
                        consume_int_flag(v_status, m_world_communicator);
                        process_running(v_status);
                        break;
                    }
                    case AVAILABLE_STATE: {
                        consume_int_flag(v_status, m_world_communicator);
                        process_available(v_status);
                        break;
                    }
                    case SCORE_PROPOSAL: {
                        score v_candidate_global_score = consume_score_flag(v_status);
                        maybe_broadcast_global_score(v_candidate_global_score, v_status);
                        break;
                    }
                    case TASK_FOR_CENTER: {
                        process_task_for_center(v_status);
                    }
                    break;
                }

                clear_buffer();
                monitor_and_notify_center_status();
            }

            spdlog::debug("CENTER HAS TERMINATED");
            spdlog::debug("Max queue size = {},   Peak memory (MB) = {} \n", m_max_queue_size, getPeakRSS() / (1024 * 1024));

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notify_termination();

            // receive solution from other processes
            receive_solution();

            m_end_time = MPI_Wtime();
            m_stats.m_elapsed_time = m_end_time - m_start_time - static_cast<double>(m_timeout);
        }

    private:
        /**
         * Only returns when a message is received from any of the communicators.
         * @return MPI_Status of the received message, or std::nullopt only when all processes have finished or a timeout occurs.
         */
        std::optional<MPI_Status> probe_communicators_at_center() {
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
                            // if no message received, check for center fullness
                            clear_buffer();
                            monitor_and_notify_center_status();
                            break;
                        }
                        case 1: {
                            if (auto v_optional = probe_score_comm_at_center(); v_optional.has_value()) {
                                return v_optional;
                            }
                            break;
                        }
                    }
                    if (++branch > INT_MAX - 1000) {
                        branch = 0;
                    }
                    ++v_cycles;
                    double v_elapsed = utils::diff_time(v_wall_time0, MPI_Wtime());
                    if (v_elapsed > m_timeout) {
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

        std::optional<MPI_Status> probe_world_comm_center() {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        std::optional<MPI_Status> probe_score_comm_at_center() {
            int v_is_message_received = 0; // logical
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, SCORE_PROPOSAL, m_global_score_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        void process_running(const MPI_Status &p_status) {
            m_process_state[p_status.MPI_SOURCE] = RUNNING_STATE; // node was assigned, now it's running
            m_nodes_running++;
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
        }

        void process_available(const MPI_Status &p_status) {
            utils::print_ipc_debug_comments("center received state_available from rank {}\n", p_status.MPI_SOURCE);
            m_process_state[p_status.MPI_SOURCE] = AVAILABLE_STATE;
            m_nodes_available++;
            m_nodes_running--;
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
        }

        /**
        * if center reaches this point, for sure workers have attained a better score,
        * or they are not up-to-date, thus it is required to broadcast it whether this value changes or not
        */
        void maybe_broadcast_global_score(const score &p_new_global_score, MPI_Status p_status) {
            utils::print_ipc_debug_comments("center received global score {} from rank {}\n", p_new_global_score.to_string(), p_status.MPI_SOURCE);

            const bool v_should_broadcast = should_broadcast_global(m_goal, m_global_score, p_new_global_score);
            if (!v_should_broadcast) {
                static int failures = 0;
                failures++;
                spdlog::debug("FAILED updates : {}, m_global_score : {} by rank {}\n", failures, m_global_score.to_string(), p_status.MPI_SOURCE);
                return;
            }

            m_global_score = p_new_global_score;
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                MPI_Send(&m_global_score, sizeof(score), MPI_BYTE, v_rank, SCORE_UPDATE, m_global_score_communicator);
            }

            static int success = 0;
            success++;
            spdlog::debug("SUCCESSFUL updates: {}, m_global_score updated to : {} by rank {}\n", success, m_global_score.to_string(), p_status.MPI_SOURCE);
        }

        void process_task_for_center(MPI_Status p_status) {
            int v_buffer_char_count;
            MPI_Get_count(&p_status, MPI_BYTE, &v_buffer_char_count);
            task_packet v_task(v_buffer_char_count);
            MPI_Recv(v_task.data(), v_buffer_char_count, MPI_BYTE, p_status.MPI_SOURCE, p_status.MPI_TAG, m_world_communicator, &p_status);

            m_center_queue.push(v_task);

            if (m_center_queue.size() > m_max_queue_size) {
                if (m_center_queue.size() % 10000 == 0) {
                    spdlog::debug("CENTER queue size reached {}\n", m_center_queue.size());
                }
                m_max_queue_size = m_center_queue.size();
            }

            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;


            if (m_center_queue.size() > 2 * CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                if (utils::diff_time(m_time_centerfull_sent, MPI_Wtime() > 1)) {
                    spdlog::debug("Center queue size is twice the limit.  Contacting workers to let them know.\n");
                    m_center_last_full_status = false; //handleFullMessaging will see this and recontact workers
                }
            }

            utils::print_ipc_debug_comments("center received task from {}, current queue size is {}\n", p_status.MPI_SOURCE, m_center_queue.size());
        }

        void monitor_and_notify_center_status() {
            const size_t v_current_memory = getCurrentRSS() / (1024 * 1024); // ram usage in megabytes

            if (m_center_last_full_status) {
                notify_center_free(v_current_memory);
            } else {
                notify_center_full(v_current_memory);
            }
        }

        void notify_center_full(const size_t v_current_memory) {
            // last iter, center wasn't full but now it is => warn nodes to stop sending
            if (v_current_memory > MAX_MEMORY_MB || m_center_queue.size() > CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                for (int rank = 1; rank < m_world_size; rank++) {
                    MPI_Send(nullptr, 0, MPI_BYTE, rank, CENTER_IS_FULL, m_center_fullness_communicator);
                }
                m_center_last_full_status = true;
                m_time_centerfull_sent = MPI_Wtime();

                //cout << "CENTER IS FULL" << endl;
            }
        }

        void notify_center_free(const size_t v_current_memory) {
            // last iter, center was full but now it has space => warn others it's ok
            if (v_current_memory <= 0.9 * MAX_MEMORY_MB && m_center_queue.size() < CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size * 0.8) {
                for (int rank = 1; rank < m_world_size; rank++) {
                    MPI_Send(nullptr, 0, MPI_BYTE, rank, CENTER_IS_FREE, m_center_fullness_communicator);
                }
                m_center_last_full_status = false;

                //cout << "CENTER IS NOT FULL ANYMORE" << endl;
            }
        }

        void notify_termination() {
            for (int rank = 1; rank < m_world_size; rank++) {
                MPI_Send(nullptr, 0, MPI_BYTE, rank, TERMINATION, m_world_communicator); // send positive signal
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

                utils::print_ipc_debug_comments("fetching result from rank {} \n", rank);

                switch (status.MPI_TAG) {
                    case HAS_RESULT: {
                        score v_score{};
                        MPI_Recv(&v_score, sizeof(score), MPI_BYTE, rank, HAS_RESULT, m_world_communicator, &status);

                        m_best_results[rank] = result{v_score, v_task_packet};
                        spdlog::debug("solution received from rank {}, count : {}, global score {} \n", rank, count, v_score.to_string());
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
            constexpr int v_dest = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[v_dest] = RUNNING_STATE;
            // *********************************************

            int err = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, v_dest, TASK_FROM_CENTER, m_world_communicator); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
        }

    private:
        int m_received_tasks = 0;
        int m_sent_tasks = 0;
        size_t m_total_requests_number = 0;
        double m_start_time = 0;
        double m_end_time = 0;
        double m_idle_time = 0;

        int m_world_rank; // get the rank of the process
        int m_world_size; // get the number of processes/nodes
        char m_processor_name[128]; // name of the node

        int m_nodes_running = 0;
        int m_nodes_available = 0;
        std::vector<int> m_process_state; // state of the nodes: running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree m_process_tree;

        std::mutex m_mutex;
        std::atomic<bool> m_transmitting;
        int m_destination_rank = -1;

        Queue<task_packet *> m_tasks_queue;

        MPI_Comm m_global_score_communicator; // BIDIRECTIONAL
        MPI_Comm m_center_fullness_communicator; // CENTER TO WORKER (ONLY)
        MPI_Comm m_world_communicator; // BIDIRECTIONAL

        score m_global_score;

        bool m_is_center_full = false;

        goal m_goal = MAXIMISE;

        std::vector<result> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        const double m_timeout; // seconds
        default_mpi_stats m_stats{-1};

        /* singleton*/
        explicit mpi_centralized_scheduler(const double p_timeout) :
            m_timeout(p_timeout) {
            if (m_timeout <= 0) {
                spdlog::throw_spdlog_ex(fmt::format("Timeout must be greater than 0, got: {:.8f}", m_timeout));
            }
            init_mpi();
            init_member_variables();
            m_stats = default_mpi_stats(m_world_rank);

            spdlog::info("MPI Scheduler, instantiated!\n");
        }

        void init_mpi() {
            set_thread_support_level();
            create_mpi_communicators();

            int v_name_length;
            MPI_Get_processor_name(m_processor_name, &v_name_length);
            spdlog::info("Process {} of {} is on {}\n", m_world_rank, m_world_size, m_processor_name);
        }

        void set_thread_support_level() {
            // Initialize MPI and ask for thread support
            int *argc = nullptr;
            char **argv = nullptr;
            int v_provided;
            MPI_Init_thread(argc, &argv, MPI_THREAD_FUNNELED, &v_provided);

            if (v_provided < MPI_THREAD_FUNNELED) {
                spdlog::info("The threading support level is lesser than that demanded.\n");
                MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
            }
        }

        void create_mpi_communicators() {
            MPI_Comm_dup(MPI_COMM_WORLD, &m_world_communicator); // world communicator for this library
            MPI_Comm_dup(MPI_COMM_WORLD, &m_global_score_communicator); // exclusive communicator for score
            MPI_Comm_dup(MPI_COMM_WORLD, &m_center_fullness_communicator);

            MPI_Comm_size(m_world_communicator, &m_world_size);
            MPI_Comm_rank(m_world_communicator, &m_world_rank);
            MPI_Barrier(m_world_communicator);

            #if !GEMPBA_DEV_MODE
            if (m_world_size < 2) {
                spdlog::info("At least two processes required !!\n");
                MPI_Abort(m_world_communicator, EXIT_FAILURE);
            }
            #endif
        }

        void init_member_variables() {
            m_process_state.resize(m_world_size, AVAILABLE_STATE);
            m_process_tree.resize(m_world_size);
            m_global_score = score::make(INT_MIN);
            m_transmitting = false;
            if (m_world_rank == 0) {
                m_best_results.resize(m_world_size, result::EMPTY);
            }
        }

        void finalize() {
            utils::print_ipc_debug_comments("rank {}, before deallocate \n", m_world_rank);
            deallocate_mpi();
            utils::print_ipc_debug_comments("rank {}, after deallocate \n", m_world_rank);
            MPI_Finalize();
            utils::print_ipc_debug_comments("rank {}, after MPI_Finalize() \n", m_world_rank);
        }

        void deallocate_mpi() {
            MPI_Comm_free(&m_global_score_communicator);
            MPI_Comm_free(&m_center_fullness_communicator);
            MPI_Comm_free(&m_world_communicator);
        }

        /* ---------------------------------------------------------------------------------
         * utility functions to determine whether to update global or local score
         * ---------------------------------------------------------------------------------*/

        static bool should_update_global(const goal p_goal, const score &p_global_score, const score &p_local_score) {
            return should_update(p_goal, p_global_score, p_local_score);
        }

        static bool should_update_local(const goal p_goal, const score &p_global_score, const score &p_local_score) {
            return should_update(p_goal, p_local_score, p_global_score);
        }

        static bool should_broadcast_global(const goal p_goal, const score &p_old_global, const score &p_new_global) {
            return should_update(p_goal, p_old_global, p_new_global);
        }

        static bool should_update(const goal p_goal, const score &p_old_value, const score &p_new_value) {
            const bool new_max_is_better = p_goal == MAXIMISE && p_new_value > p_old_value;
            const bool new_min_is_better = p_goal == MINIMISE && p_new_value < p_old_value;
            return new_max_is_better || new_min_is_better;
        }

    };

}

#endif
