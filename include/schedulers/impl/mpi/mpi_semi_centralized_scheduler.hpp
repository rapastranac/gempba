#ifndef GEMPBA_MPI_SEMI_CENTRALIZED_SCHEDULER_HPP
#define GEMPBA_MPI_SEMI_CENTRALIZED_SCHEDULER_HPP

#include <algorithm>
#include <atomic>
#include <cfloat>
#include <climits>
#include <cstdlib> /* srand, rand */
#include <memory>
#include <mpi.h>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>

#include <result_holder/result_holder_parent.hpp>
#include <schedulers/api/scheduler.hpp>
#include <schedulers/impl/mpi/default_mpi_stats.hpp>
#include <spdlog/spdlog.h>
#include <utils/Queue.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/tree.hpp>
#include <utils/utils.hpp>
#include <utils/ipc/result.hpp>
#include <utils/ipc/task_packet.hpp>

namespace gempba {

    class branch_handler;

    // inter process communication handler
    class mpi_semi_centralized_scheduler final : public scheduler {

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
            TASK = 10,
        };

    public:
        ~mpi_semi_centralized_scheduler() override {
            // MPI specific finalization
            finalize();
            spdlog::info("Goodbye from MPI Semi Centralized Scheduler");
        }

        static mpi_semi_centralized_scheduler &get_instance(const double p_timeout = 3.0) {
            static mpi_semi_centralized_scheduler instance(p_timeout);
            return instance;
        }

        [[nodiscard]] int rank_me() const override {
            return m_world_rank;
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

        [[nodiscard]] std::vector<std::unique_ptr<stats> > get_stats_vector() const override {
            std::vector<std::unique_ptr<stats> > v_stats_vector;
            for (const auto &v_stats: m_stats_vector) {
                v_stats_vector.emplace_back(std::make_unique<default_mpi_stats>(v_stats));
            }
            return v_stats_vector;
        }

        void synchronize_stats() override {
            barrier();
            constexpr int v_dummy_tag = 999;
            if (m_world_rank == 0) {
                m_stats_vector.push_back(m_stats);
                for (int v_src_rank = 1; v_src_rank < m_world_size; ++v_src_rank) {
                    MPI_Status v_status;
                    const int v_probe_err = MPI_Probe(v_src_rank, v_dummy_tag, m_world_communicator, &v_status);
                    if (v_probe_err != MPI_SUCCESS) {
                        spdlog::error("rank {} failed to probe message from rank {}\n", m_world_rank, v_src_rank);
                        continue;
                    }

                    int v_count{};
                    MPI_Get_count(&v_status, MPI_BYTE, &v_count);
                    task_packet v_rank_packet(v_count);
                    const int v_recv_err = MPI_Recv(v_rank_packet.data(), v_count, MPI_BYTE, v_src_rank, v_dummy_tag, m_world_communicator, &v_status);
                    if (v_recv_err != MPI_SUCCESS) {
                        spdlog::error("rank {} failed to receive stats from rank {}\n", m_world_rank, v_src_rank);
                        continue;
                    }
                    default_mpi_stats v_rank_stats = default_mpi_stats::from_packet(v_rank_packet);
                    m_stats_vector.push_back(std::move(v_rank_stats));
                }
            } else {
                auto v_bytes = m_stats.serialize();
                const int v_err = MPI_Ssend(v_bytes.data(), static_cast<int>(v_bytes.size()), MPI_BYTE, CENTER_NODE, v_dummy_tag, m_world_communicator);
                if (v_err != MPI_SUCCESS) {
                    spdlog::error("rank {} failed to send stats to rank 0\n", m_world_rank);
                }
            }
            barrier();
        }

        [[nodiscard]] double elapsed_time() const override {
            return (m_end_time - m_start_time) - static_cast<double>(m_timeout);
        }

    private:
        [[nodiscard]] int next_process() const {
            return this->m_next_process;
        }

    public:
        [[nodiscard]] int world_size() const override {
            return m_world_size;
        }

        void barrier() override {
            if (m_world_communicator != MPI_COMM_NULL) {
                MPI_Barrier(m_world_communicator);
            }
        }

    private:
        bool try_open_transmission_channel() {
            // acquires mutex
            if (m_mutex.try_lock()) {
                // check if transmission in progress
                if (!m_transmitting.load()) {
                    const int v_next = next_process();
                    // check if there is another process in the list
                    if (v_next > 0) {
                        return true; // priority acquired "mutex is meant to be released in releasePriority() "
                    }
                }
                m_mutex.unlock();
            }
            return false;
        }

        /* this should be invoked only if channel is open*/
        void close_transmission_channel() {
            m_mutex.unlock();
        }

    public:
        void set_goal(const goal p_goal, const score_type p_type) override {
            this->m_goal = p_goal;
            this->m_global_score = utils::get_default_score(p_goal, p_type);
        }

    private:
        void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                      std::function<result()> &p_result_fetcher) {
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
                    case NEXT_PROCESS: {
                        receive_next_process(v_status);
                        break;
                    }
                    case TASK: {
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
        void push(task_packet &&p_task_packet) {
            if (p_task_packet.empty()) {
                throw std::runtime_error(fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank));
            }

            m_transmitting = true;
            m_destination_rank = next_process();
            utils::print_ipc_debug_comments("rank {} entered MPI_Scheduler::push(..) for the node {}\n", m_world_rank, m_destination_rank);
            utils::shift_left(m_next_processes);

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

        void receive_next_process(MPI_Status p_status) {
            int v_count;
            MPI_Get_count(&p_status, MPI_INT, &v_count); // 0 < count < world_size   -- safe
            utils::print_ipc_debug_comments("rank {}, about to receive nextProcess from Center, count : {}\n", m_world_rank, v_count);
            MPI_Recv(m_next_processes.data(), v_count, MPI_INT, CENTER_NODE, NEXT_PROCESS, m_next_process_communicator, &p_status);
            utils::print_ipc_debug_comments("rank {}, received nextProcess from Center, count : {}\n", m_world_rank, v_count);
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

        void send_task(task_packet &p_task_packet) {
            const size_t v_message_length = p_task_packet.size();
            if (v_message_length > std::numeric_limits<int>::max()) {
                throw std::runtime_error("message is to long to be sent in a single message, currently not supported");
            }

            if (m_destination_rank > 0) {
                if (m_destination_rank == m_world_rank) {
                    throw std::runtime_error("rank " + std::to_string(m_world_rank) + " attempting to send to itself !!!\n");
                }
                utils::print_ipc_debug_comments("rank {} about to send buffer to rank {}\n", m_world_rank, m_destination_rank);
                MPI_Send(p_task_packet.data(), static_cast<int>(v_message_length), MPI_BYTE, m_destination_rank, TASK, m_world_communicator);
                utils::print_ipc_debug_comments("rank {} sent buffer to rank {}\n", m_world_rank, m_destination_rank);
                m_destination_rank = -1;
                m_sent_tasks++;
                m_total_requests_number++;
                m_stats.m_sent_task_count++;
                m_stats.m_total_requested_tasks++;
            } else {
                throw std::runtime_error("rank " + std::to_string(m_world_rank) + ", could not send task to rank " + std::to_string(m_destination_rank) + "\n");
            }
        }

        // it separates receiving buffer from the local
        void update_next_process() {
            m_next_process = m_next_processes[0];
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

        /*	run the center node */
        void run_center(task_packet &p_seed) {
            task_packet v_task_packet = p_seed;
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();

            if (!m_custom_initial_topology) {
                utils::build_topology(m_process_tree, 1, 0, 2, m_world_size);
            }
            broadcast_nodes_topology();
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
            m_stats.m_elapsed_time = m_end_time - m_start_time - static_cast<double>(m_timeout);
        }

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
            ++m_nodes_running;
            utils::print_ipc_debug_comments("rank {} reported running, nRunning :{}\n", p_status.MPI_SOURCE, m_nodes_running);

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
            m_stats.m_total_requested_tasks++;
        }

        void process_available(const MPI_Status &p_status) {
            m_process_state[p_status.MPI_SOURCE] = AVAILABLE_STATE;
            ++m_nodes_available;
            --m_nodes_running;
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;

            if (m_process_tree[p_status.MPI_SOURCE].is_assigned()) {
                m_process_tree[p_status.MPI_SOURCE].release();
            }
            utils::print_ipc_debug_comments("rank {} reported available, nRunning :{}\n", p_status.MPI_SOURCE, m_nodes_running);
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
                utils::print_ipc_debug_comments("ASSIGNMENT:\trank {} <-- [{}]\n", v_rank, p_status.MPI_SOURCE);
                break; // IMPORTANT: breaks for-loop
            }
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
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
        }

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

            int err = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, v_dest, TASK, m_world_communicator); // send buffer
            if (err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
            m_sent_tasks++;
            m_total_requests_number++;
            m_stats.m_sent_task_count++;
            m_stats.m_total_requested_tasks++;
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
        MPI_Comm m_next_process_communicator; // CENTER TO WORKER (ONLY)
        MPI_Comm m_world_communicator; // BIDIRECTIONAL

        score m_global_score;

        std::vector<int> m_next_processes;
        int m_next_process = -1;

        goal m_goal = MAXIMISE;

        std::vector<result> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        const double m_timeout; // seconds
        default_mpi_stats m_stats{-1};
        std::vector<default_mpi_stats> m_stats_vector;

        /* singleton*/
        explicit mpi_semi_centralized_scheduler(const double p_timeout) :
            m_timeout(p_timeout) {
            if (m_timeout <= 0) {
                spdlog::throw_spdlog_ex(fmt::format("Timeout must be greater than 0, got: {:.8f}", m_timeout));
            }
            init_mpi();
            init_member_variables();
            m_stats = default_mpi_stats(m_world_rank);
            m_stats_vector.reserve(m_world_size);

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
            MPI_Comm_dup(MPI_COMM_WORLD, &m_global_score_communicator); // exclusive communicator for reference value - one-sided comm
            MPI_Comm_dup(MPI_COMM_WORLD, &m_next_process_communicator); // exclusive communicator for next process - one-sided comm

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
            m_next_processes.resize(m_world_size, -1);
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
            MPI_Comm_free(&m_next_process_communicator);
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


        class center_view final : public center {

            mpi_semi_centralized_scheduler &m_parent;

        public:
            explicit center_view(mpi_semi_centralized_scheduler &p_parent) :
                m_parent(p_parent) {
            }

            ~center_view() override = default;

            void barrier() override {
                m_parent.barrier();
            }

            [[nodiscard]] int rank_me() const override {
                return m_parent.rank_me();
            }

            [[nodiscard]] int world_size() const override {
                return m_parent.world_size();
            }

            [[nodiscard]] std::unique_ptr<stats> get_stats() const override {
                return m_parent.get_stats();
            }

            void run(task_packet p_task) override {
                m_parent.run_center(p_task);
            }

            task_packet get_result() override {
                return m_parent.fetch_solution();
            }

            std::vector<result> get_all_results() override {
                return m_parent.fetch_result_vector();
            }
        };

        class worker_view final : public worker {
            mpi_semi_centralized_scheduler &m_parent;

        public:
            explicit worker_view(mpi_semi_centralized_scheduler &p_parent) :
                m_parent(p_parent) {
            }

            ~worker_view() override = default;

            void barrier() override {
                m_parent.barrier();
            }

            [[nodiscard]] int rank_me() const override {
                return m_parent.rank_me();
            }

            [[nodiscard]] int world_size() const override {
                return m_parent.world_size();
            }

            [[nodiscard]] std::unique_ptr<stats> get_stats() const override {
                return m_parent.get_stats();
            }

            void run(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                     std::function<result()> &p_result_fetcher) override {
                m_parent.run_node(p_branch_handler, p_buffer_decoder, p_result_fetcher);
            }

            void push(task_packet &&p_task) override {
                m_parent.push(std::move(p_task));
            }

            [[nodiscard]] unsigned int next_process() const override {
                return m_parent.next_process();
            }

            bool try_open_transmission_channel() override {
                return m_parent.try_open_transmission_channel();
            }

            void close_transmission_channel() override {
                m_parent.close_transmission_channel();
            }
        };

        // scheduler views
        center_view m_center_view{*this};
        worker_view m_worker_view{*this};

    public:
        // ————————— ↓↓↓↓  New development ↓↓↓↓ ——————————
        center &center_view() override {
            if (m_world_rank != 0) {
                spdlog::throw_spdlog_ex("Only rank 0 can access center_view()");
            }
            return m_center_view;
        }

        worker &worker_view() override {
            if (m_world_rank == 0) {
                spdlog::throw_spdlog_ex("Rank 0 cannot access worker_view()");
            }
            return m_worker_view;
        }
    };

}

#endif
