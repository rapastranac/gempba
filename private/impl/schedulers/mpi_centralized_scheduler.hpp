#ifndef GEMPBA_MPI_SCHEDULER_CENTRALIZED_HPP
#define GEMPBA_MPI_SCHEDULER_CENTRALIZED_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cstdlib> /* srand, rand */
#include <memory>
#include <mpi.h>
#include <optional>
#include <queue>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

#include <gempba/core/scheduler.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gempba/utils/gempba_utils.hpp>
#include <gempba/utils/queue.hpp>
#include <gempba/utils/result.hpp>
#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>
#include <gempba/utils/tree.hpp>
#include <gempba/utils/utils.hpp>
#include <impl/schedulers/centralized_utils.hpp>
#include <impl/schedulers/default_mpi_stats.hpp>
#include <impl/schedulers/termination_detector.hpp>

// max memory is in mb, e.g. 1024 * 10 = 10 GB
#define MAX_MEMORY_MB (1024 * 10)
#define CENTER_NBSTORED_TASKS_PER_PROCESS 1000

namespace gempba {

    class node_manager;

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
            RUNNABLE_ID = 14,
            TASK_RECEIVED = 15,
        };

        std::priority_queue<task_bundle, std::vector<task_bundle>, task_bundle_comparator> m_center_bundle_queue; // message, size

        int m_max_queue_size{};
        bool m_center_last_full_status = false;
        double m_time_centerfull_sent = 0;

    public:
        ~mpi_centralized_scheduler() override {
            // MPI specific finalization
            finalize();
            spdlog::debug("Goodbye from MPI Centralized Scheduler");
        }

        mpi_centralized_scheduler() {
            init_mpi();
            init_member_variables();
            m_stats = default_mpi_stats(m_world_rank);
            m_stats_vector.reserve(m_world_size);

            spdlog::debug("MPI Scheduler, instantiated!\n");
        }

        [[nodiscard]] int rank_me() const override { return m_world_rank; }

        void set_custom_initial_topology(tree&& p_tree) override {
            m_process_tree = std::move(p_tree);
            m_custom_initial_topology = true;
        }

    private:
        task_packet fetch_solution() {
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                if (m_best_results[v_rank].get_score() == m_global_score) {
                    return m_best_results[v_rank].get_task_packet();
                }
            }
            return task_packet::EMPTY; // no solution found
        }

        std::vector<result> fetch_result_vector() { return m_best_results; }

    public:
        [[nodiscard]] std::unique_ptr<stats> get_stats() const override { return std::make_unique<default_mpi_stats>(m_stats); }

        [[nodiscard]] std::vector<std::unique_ptr<stats>> get_stats_vector() const override {
            std::vector<std::unique_ptr<stats>> v_stats_vector;
            v_stats_vector.reserve(m_stats_vector.size());
            for (const auto& v_stats: m_stats_vector) {
                v_stats_vector.emplace_back(std::make_unique<default_mpi_stats>(v_stats));
            }
            return v_stats_vector;
        }

        void synchronize_stats() override {
            barrier();
            constexpr int DUMMY_TAG = 999;
            if (m_world_rank == 0) {
                m_stats_vector.push_back(m_stats);
                for (int v_src_rank = 1; v_src_rank < m_world_size; ++v_src_rank) {
                    MPI_Status v_status;
                    const int v_probe_err = MPI_Probe(v_src_rank, DUMMY_TAG, m_world_communicator, &v_status);
                    if (v_probe_err != MPI_SUCCESS) {
                        spdlog::error("rank {} failed to probe message from rank {}\n", m_world_rank, v_src_rank);
                        continue;
                    }

                    int v_count{};
                    MPI_Get_count(&v_status, MPI_BYTE, &v_count);
                    task_packet v_rank_packet(v_count);
                    const int v_recv_err = MPI_Recv(v_rank_packet.data(), v_count, MPI_BYTE, v_src_rank, DUMMY_TAG, m_world_communicator, &v_status);
                    if (v_recv_err != MPI_SUCCESS) {
                        spdlog::error("rank {} failed to receive stats from rank {}\n", m_world_rank, v_src_rank);
                        continue;
                    }
                    default_mpi_stats v_rank_stats = default_mpi_stats::from_packet(v_rank_packet);
                    m_stats_vector.push_back(std::move(v_rank_stats));
                }
            } else {
                auto v_bytes = m_stats.serialize();
                const int v_err = MPI_Ssend(v_bytes.data(), static_cast<int>(v_bytes.size()), MPI_BYTE, CENTER_NODE, DUMMY_TAG, m_world_communicator);
                if (v_err != MPI_SUCCESS) {
                    spdlog::error("rank {} failed to send stats to rank 0\n", m_world_rank);
                }
            }
            barrier();
        }

        [[nodiscard]] double elapsed_time() const override { return m_end_time - m_start_time; }

        [[nodiscard]] std::size_t get_pending_request_count() const override { return m_center_bundle_queue.size(); }

        [[nodiscard]] int world_size() const override { return m_world_size; }

        void barrier() override {
            if (m_world_communicator != MPI_COMM_NULL) {
                MPI_Barrier(m_world_communicator);
            }
        }

    private:
        std::optional<transmission_guard> try_open_transmission_channel() {
            std::unique_lock v_lock(m_mutex, std::try_to_lock);
            // acquires mutex
            if (!v_lock.owns_lock()) {
                return std::nullopt;
            }
            // check if transmission in progress
            if (m_transmitting.load()) {
                return std::nullopt;
            }
            // check if center is actually waiting for a task
            if (!m_is_center_full) {
                return transmission_guard(std::move(v_lock));
            }
            return std::nullopt;
        }

    public:
        void set_goal(const goal p_goal, const score_type p_type) override {
            this->m_goal = p_goal;
            this->m_global_score = utils::get_default_score(p_goal, p_type);
        }

    private:
        void run(node_manager& p_node_manager, std::map<int, std::shared_ptr<serial_runnable>> p_runnables) {
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_run_start();
                v_telemetry->record_worker_waiting();
            }

            bool v_is_terminated = false;
            while (true) {
                MPI_Status v_status = probe_communicators_at_worker();

                switch (v_status.MPI_TAG) {
                    case TERMINATION: {
                        process_termination(v_status);
                        collect_stats_data(p_node_manager);
                        v_is_terminated = true; // temporary, it should always happen
                        spdlog::debug("rank {} received termination", m_world_rank);
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
                        process_message(v_status, p_node_manager, p_runnables);
                        break;
                    }
                    case TASK_RECEIVED: {
                        process_task_received_ack(v_status);
                        release_parent_if_passive();
                        break;
                    }
                    default: {
                        // nothing
                    };
                }

                if (v_is_terminated) {
                    break; // exit loop
                }

                if (auto* v_telemetry = telemetry::get())
                    v_telemetry->tick_if_due();
            }
            /**
             * TODO.. send results back to the rank from which the task was sent.
             * this applies only when parallelising non-void functions
             */

            send_final_solution_to_center(p_node_manager);
            m_end_time = MPI_Wtime();
            m_stats.m_elapsed_time = m_end_time - m_start_time;
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_run_end();
            }
        }

        void send_final_solution_to_center(node_manager& p_node_manager) const;

        unsigned int force_push(task_packet&& p_task_packet, const int p_function_id) {
            if (p_task_packet.empty()) {
                throw std::runtime_error(fmt::format("rank {}, attempted to send empty buffer \n", m_world_rank));
            }

            m_transmitting = true;
            utils::print_ipc_debug_comments("rank {} entered MPI_Scheduler::push(..) for the node {}\n", m_world_rank, m_destination_rank);

            const auto v_bundle = std::make_shared<task_bundle>(p_task_packet, static_cast<unsigned int>(p_function_id));

            if (!m_tasks_bundle_queue.empty()) {
                throw std::runtime_error("ERROR: q is not empty !!!!\n");
            }

            m_tasks_bundle_queue.push(v_bundle);
            return 0;
        }

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

                // Drive telemetry from inside the busy-wait so worker frames
                // ship in real time even when no task traffic is flowing.
                if (auto* v_telemetry = telemetry::get())
                    v_telemetry->tick_if_due();
            }
        }

        void process_termination(MPI_Status p_status) {
            std::array<int, 2> v_sent_counts{};
            MPI_Recv(v_sent_counts.data(), static_cast<int>(v_sent_counts.size()), MPI_INT, p_status.MPI_SOURCE, TERMINATION, m_world_communicator, &p_status);
            utils::print_ipc_debug_comments("rank {}, received message from rank {}", m_world_rank, p_status.MPI_SOURCE);
            if (!m_termination.is_quiescent() || !m_tasks_bundle_queue.empty() || m_transmitting.load()) {
                utils::log_and_throw("rank {} received termination with work in progress: deficit {}, parent {}", m_world_rank, m_termination.deficit(), m_termination.parent());
            }
            drain_center_messages(v_sent_counts[0], v_sent_counts[1]);

            spdlog::debug("rank {} exited\n", m_world_rank);
            MPI_Barrier(m_world_communicator);
        }

        void drain_center_messages(const int p_score_updates_sent, const int p_fullness_notices_sent) {
            const int v_pending_score_updates = p_score_updates_sent - m_score_updates_received;
            const int v_pending_fullness_notices = p_fullness_notices_sent - m_fullness_notices_received;
            while (m_score_updates_received < p_score_updates_sent) {
                MPI_Status v_status;
                MPI_Probe(CENTER_NODE, SCORE_UPDATE, m_global_score_communicator, &v_status);
                receive_score_from_center(v_status);
            }
            while (m_fullness_notices_received < p_fullness_notices_sent) {
                MPI_Status v_status;
                MPI_Probe(CENTER_NODE, MPI_ANY_TAG, m_center_fullness_communicator, &v_status);
                if (v_status.MPI_TAG == CENTER_IS_FULL) {
                    consume_center_is_full_tag(v_status);
                } else {
                    consume_center_is_free_tag(v_status);
                }
            }
            if (m_score_updates_received != p_score_updates_sent || m_fullness_notices_received != p_fullness_notices_sent) {
                utils::log_and_throw("rank {} received more center messages than the center sent: score updates {}/{}, fullness notices {}/{}", m_world_rank, m_score_updates_received,
                                     p_score_updates_sent, m_fullness_notices_received, p_fullness_notices_sent);
            }
            spdlog::debug("rank {} drained {} score updates and {} fullness notices at termination", m_world_rank, v_pending_score_updates, v_pending_fullness_notices);
        }

        void process_task_received_ack(MPI_Status p_status) {
            MPI_Recv(nullptr, 0, MPI_BYTE, p_status.MPI_SOURCE, TASK_RECEIVED, m_world_communicator, &p_status);
            if (!m_termination.on_ack_received()) {
                utils::log_and_throw("rank {} received an unexpected task acknowledgement from rank {}", m_world_rank, p_status.MPI_SOURCE);
            }
        }

        void send_task_ack(const int p_destination) const { MPI_Send(nullptr, 0, MPI_BYTE, p_destination, TASK_RECEIVED, m_world_communicator); }

        void release_parent_if_passive() {
            if (const std::optional<int> v_parent = m_termination.on_passive()) {
                send_task_ack(*v_parent);
            }
        }

        void collect_stats_data(const node_manager& p_node_manager);

        void receive_score_from_center(MPI_Status p_status) {
            utils::print_ipc_debug_comments("rank {}, about to receive global score from Center\n", m_world_rank);
            MPI_Recv(&m_global_score, sizeof(score), MPI_BYTE, CENTER_NODE, SCORE_UPDATE, m_global_score_communicator, &p_status);
            ++m_score_updates_received;
            utils::print_ipc_debug_comments("rank {}, received global score: {} from Center\n", m_world_rank, m_global_score.to_string());
        }

        void consume_center_is_full_tag(MPI_Status p_status) {
            MPI_Recv(nullptr, 0, MPI_BYTE, CENTER_NODE, CENTER_IS_FULL, m_center_fullness_communicator, &p_status);
            ++m_fullness_notices_received;
            m_is_center_full = true;

            utils::print_ipc_debug_comments("Node {} received full center\n", rank_me());
        }

        void consume_center_is_free_tag(MPI_Status p_status) {
            MPI_Recv(nullptr, 0, MPI_BYTE, CENTER_NODE, CENTER_IS_FREE, m_center_fullness_communicator, &p_status);
            ++m_fullness_notices_received;
            m_is_center_full = false;

            utils::print_ipc_debug_comments("Node {} received free center\n", rank_me());
        }

        void process_message(MPI_Status& p_status, node_manager& p_node_manager, std::map<int, std::shared_ptr<serial_runnable>>& p_runnables) {
            // Receives the task  -------------------------------------------------------------------------------------------
            int v_count; // count to be received
            MPI_Get_count(&p_status, MPI_BYTE, &v_count); // receives total number of datatype elements of the message

            utils::print_ipc_debug_comments("rank {}, received message from rank {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, v_count);
            task_packet v_task_packet(v_count);
            const auto v_recv_start = std::chrono::steady_clock::now();
            MPI_Recv(v_task_packet.data(), v_count, MPI_BYTE, p_status.MPI_SOURCE, p_status.MPI_TAG, m_world_communicator, &p_status);

            utils::print_ipc_debug_comments("rank {}, received message from rank {}, tag {}, count : {}\n", m_world_rank, p_status.MPI_SOURCE, p_status.MPI_TAG, v_count);

            // receives function id
            MPI_Status v_status;
            int v_function_id;
            MPI_Recv(&v_function_id, 1, MPI_INT, p_status.MPI_SOURCE, RUNNABLE_ID, m_world_communicator, &v_status);
            const auto v_recv_elapsed = std::chrono::steady_clock::now() - v_recv_start;
            if (const std::optional<int> v_ack = m_termination.on_task_received(p_status.MPI_SOURCE)) {
                send_task_ack(*v_ack);
            }

            // Here, we have a task to process  -----------------------------------------------------------------------------
            notify_running_state();
            m_received_tasks++;
            m_total_requests_number++;
            m_stats.m_received_task_count++;
            m_stats.m_total_requested_tasks++;

            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_worker_running();
                v_telemetry->record_recv(static_cast<std::uint32_t>(p_status.MPI_SOURCE), static_cast<std::uint64_t>(v_count), v_recv_elapsed);
            }

            //  Delegates the processing of the incoming buffer to the node manager and stores the potential returned value in _returned_value_future.
            const auto v_runnable = p_runnables[v_function_id];
            auto v_returned_value = (*v_runnable)(p_node_manager, v_task_packet);
            utils::print_ipc_debug_comments("rank {}, pushed buffer to thread pool \n", m_world_rank, p_status.MPI_SOURCE);
            // **********************************************************************************************

            task_funneling(p_node_manager);
            notify_available_state();
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_worker_waiting();
            }
            release_parent_if_passive();
        }

        void task_funneling(node_manager& p_node_manager);

        [[nodiscard]] std::optional<MPI_Status> probe_score_comm_at_node() const {
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

        [[nodiscard]] std::optional<MPI_Status> probe_world_comm() const {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<MPI_Status> probe_center_fullness_comm() const {
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
            std::optional<MPI_Status> v_opt = probe_center_fullness_comm();
            if (!v_opt.has_value()) {
                return;
            }
            const MPI_Status v_status = v_opt.value();

            if (v_status.MPI_TAG == CENTER_IS_FULL) {
                consume_center_is_full_tag(v_status);
            } else if (v_status.MPI_TAG == CENTER_IS_FREE) {
                consume_center_is_free_tag(v_status);
            }
        }

        // if score is received, it attempts updating local value
        // if local value is better than the one in center, then the local best value is sent to center
        void update_score(node_manager& p_node_manager);

        void notify_available_state() {
            utils::print_ipc_debug_comments("rank {} entered notify_available_state()\n", m_world_rank);
            constexpr int BUFFER = 0;
            MPI_Send(&BUFFER, 1, MPI_INT, CENTER_NODE, AVAILABLE_STATE, m_world_communicator);
        }

        void notify_running_state() const {
            constexpr int BUFFER = 0;
            MPI_Send(&BUFFER, 1, MPI_INT, CENTER_NODE, RUNNING_STATE, m_world_communicator);
        }

        void send_task_to_center(task_bundle& p_bundle) {
            task_packet v_packet = p_bundle.get_task_packet();
            const int v_function_id = p_bundle.get_runnable_id();

            m_termination.on_task_sent();
            MPI_Send(v_packet.data(), static_cast<int>(v_packet.size()), MPI_BYTE, CENTER_NODE, TASK_FOR_CENTER, m_world_communicator);
            MPI_Send(&v_function_id, 1, MPI_INT, CENTER_NODE, RUNNABLE_ID, m_world_communicator);
        }

    public:
    private:
        static int consume_int_flag(MPI_Status p_status, const MPI_Comm& p_communicator) {
            int v_buffer;
            MPI_Recv(&v_buffer, 1, MPI_INT, p_status.MPI_SOURCE, p_status.MPI_TAG, p_communicator, &p_status);
            return v_buffer;
        }

        [[nodiscard]] score consume_score_flag(MPI_Status p_status) const {
            score v_score{};
            MPI_Recv(&v_score, sizeof(score), MPI_BYTE, p_status.MPI_SOURCE, p_status.MPI_TAG, m_global_score_communicator, &p_status);
            return v_score;
        }

        void clear_buffer() {
            m_nodes_available -= dispatch_to_available(m_center_bundle_queue, m_process_state, AVAILABLE_STATE, ASSIGNED_STATE, [this](const int p_rank, const task_bundle& p_bundle) {
                const task_packet v_bytes = p_bundle.get_task_packet();
                const int v_runnable_id = p_bundle.get_runnable_id();

                m_termination.on_task_sent();
                const auto v_send_start = std::chrono::steady_clock::now();
                MPI_Send(v_bytes.data(), static_cast<int>(v_bytes.size()), MPI_BYTE, p_rank, TASK_FROM_CENTER, m_world_communicator);
                MPI_Send(&v_runnable_id, 1, MPI_INT, p_rank, RUNNABLE_ID, m_world_communicator);
                const auto v_send_elapsed = std::chrono::steady_clock::now() - v_send_start;

                m_sent_tasks++;
                m_total_requests_number++;
                m_stats.m_sent_task_count++;
                m_stats.m_total_requested_tasks++;
                m_stats.m_center_messages_handled++;

                if (auto* v_telemetry = telemetry::get()) {
                    v_telemetry->record_send(static_cast<std::uint32_t>(p_rank), static_cast<std::uint64_t>(v_bytes.size()), v_send_elapsed);
                    v_telemetry->record_assignment(static_cast<std::uint32_t>(p_rank));
                }
            });
        }

        void record_center_queue() const {
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_center_queue(static_cast<std::uint32_t>(m_nodes_available), m_center_bundle_queue.empty());
            }
        }

        void run(const task_packet& p_seed, const int p_runnable_id) {
            MPI_Barrier(m_world_communicator);
            m_start_time = MPI_Wtime();
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_run_start();
            }

            send_seed(p_seed, p_runnable_id);
            record_center_queue();

            while (true) {
                MPI_Status v_status = probe_communicators_at_center();

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
                    } break;
                    case TASK_RECEIVED: {
                        process_task_received_ack(v_status);
                        break;
                    }
                }

                if (m_termination.deficit() == 0 && m_center_bundle_queue.empty()) {
                    break;
                }

                clear_buffer();
                monitor_and_notify_center_status();
                record_center_queue();

                if (auto* v_telemetry = telemetry::get())
                    v_telemetry->tick_if_due();
            }

            spdlog::debug("CENTER HAS TERMINATED");
            spdlog::debug("Max queue size = {},   Peak memory (MB) = {} \n", m_max_queue_size, get_peak_rss() / (1024 * 1024));

            /*
            after breaking the previous loop, all jobs are finished and the only remaining step
            is notifying exit and fetching results
            */
            notify_termination();

            // receive solution from other processes
            receive_solution();

            m_end_time = MPI_Wtime();
            m_stats.m_elapsed_time = m_end_time - m_start_time;
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_run_end();
            }
        }

        MPI_Status probe_communicators_at_center() {
            static int branch = 0;
            while (true) {
                switch (branch % 2) {
                    case 0: {
                        if (const auto v_optional = probe_world_comm_center(); v_optional.has_value()) {
                            return v_optional.value();
                        }
                        // if no message received, check for center fullness
                        clear_buffer();
                        monitor_and_notify_center_status();
                        break;
                    }
                    case 1: {
                        if (const auto v_optional = probe_score_comm_at_center(); v_optional.has_value()) {
                            return v_optional.value();
                        }
                        break;
                    }
                }
                if (++branch > INT_MAX - 1000) {
                    branch = 0;
                }

                // Drive telemetry from inside the center busy-wait so the
                // aggregator drains worker frames in real time even when
                // no task traffic is flowing.
                if (auto* v_telemetry = telemetry::get()) {
                    v_telemetry->tick_if_due();
                }
            }
        }

        [[nodiscard]] std::optional<MPI_Status> probe_world_comm_center() const {
            int v_is_message_received = 0;
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, MPI_ANY_TAG, m_world_communicator, &v_is_message_received, &v_status); // for regular messages
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<MPI_Status> probe_score_comm_at_center() const {
            int v_is_message_received = 0; // logical
            MPI_Status v_status;
            MPI_Iprobe(MPI_ANY_SOURCE, SCORE_PROPOSAL, m_global_score_communicator, &v_is_message_received, &v_status);
            if (v_is_message_received) {
                return v_status;
            }
            return std::nullopt;
        }

        void process_running(const MPI_Status& p_status) {
            m_process_state[p_status.MPI_SOURCE] = RUNNING_STATE; // node was assigned, now it's running
            m_nodes_running++;
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
            m_stats.m_center_messages_handled++;
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_center_state_msg();
                v_telemetry->record_assignment_accepted(static_cast<std::uint32_t>(p_status.MPI_SOURCE));
            }
        }

        void process_available(const MPI_Status& p_status) {
            utils::print_ipc_debug_comments("center received state_available from rank {}\n", p_status.MPI_SOURCE);
            m_process_state[p_status.MPI_SOURCE] = AVAILABLE_STATE;
            m_nodes_available++;
            m_nodes_running--;
            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
            m_stats.m_center_messages_handled++;
            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_center_state_msg();
            }
        }

        /**
         * if center reaches this point, for sure workers have attained a better score,
         * or they are not up-to-date, thus it is required to broadcast it whether this value changes or not
         */
        void maybe_broadcast_global_score(const score& p_new_global_score, MPI_Status p_status) {
            utils::print_ipc_debug_comments("center received global score {} from rank {}\n", p_new_global_score.to_string(), p_status.MPI_SOURCE);
            auto* v_telemetry = telemetry::get();
            m_stats.m_center_messages_handled++;
            if (v_telemetry) {
                v_telemetry->record_center_score_proposal();
            }

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
            ++m_score_updates_sent;
            m_stats.m_center_messages_handled++;
            if (v_telemetry) {
                v_telemetry->record_center_score_broadcast();
                v_telemetry->record_global_score_improved();
            }

            static int success = 0;
            success++;
            spdlog::debug("SUCCESSFUL updates: {}, m_global_score updated to : {} by rank {}\n", success, m_global_score.to_string(), p_status.MPI_SOURCE);

            m_total_requests_number++;
            m_stats.m_total_requested_tasks++;
        }

        void process_task_for_center(MPI_Status p_status) {
            int v_buffer_char_count;
            MPI_Get_count(&p_status, MPI_BYTE, &v_buffer_char_count);
            task_packet v_task(v_buffer_char_count);
            const auto v_recv_start = std::chrono::steady_clock::now();
            MPI_Recv(v_task.data(), v_buffer_char_count, MPI_BYTE, p_status.MPI_SOURCE, TASK_FOR_CENTER, m_world_communicator, &p_status);

            int v_runnable_id;
            MPI_Status v_status;
            MPI_Recv(&v_runnable_id, 1, MPI_INT, p_status.MPI_SOURCE, RUNNABLE_ID, m_world_communicator, &v_status);
            const auto v_recv_elapsed = std::chrono::steady_clock::now() - v_recv_start;
            const task_bundle v_task_bundle(v_task, v_runnable_id);

            m_center_bundle_queue.push(v_task_bundle);
            if (const std::optional<int> v_ack = m_termination.on_task_received(p_status.MPI_SOURCE)) {
                send_task_ack(*v_ack);
            }

            if (m_center_bundle_queue.size() > m_max_queue_size) {
                if (m_center_bundle_queue.size() % 10000 == 0) {
                    spdlog::debug("CENTER queue size reached {}\n", m_center_bundle_queue.size());
                }
                m_max_queue_size = static_cast<int>(m_center_bundle_queue.size());
            }

            m_total_requests_number++;
            m_received_tasks++;
            m_stats.m_total_requested_tasks++;
            m_stats.m_received_task_count++;
            m_stats.m_center_messages_handled++;

            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_recv(static_cast<std::uint32_t>(p_status.MPI_SOURCE), static_cast<std::uint64_t>(v_buffer_char_count), v_recv_elapsed);
            }


            if (m_center_bundle_queue.size() > 2 * CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                if (utils::diff_time(m_time_centerfull_sent, MPI_Wtime()) > 1) {
                    spdlog::debug("Center queue size is twice the limit.  Contacting workers to let them know.\n");
                    m_center_last_full_status = false; // handleFullMessaging will see this and recontact workers
                }
            }

            utils::print_ipc_debug_comments("center received task from {}, current queue size is {}\n", p_status.MPI_SOURCE, m_center_bundle_queue.size());
        }


        void monitor_and_notify_center_status() {
            const size_t v_current_memory = get_current_rss() / (1024 * 1024); // ram usage in megabytes

            if (m_center_last_full_status) {
                notify_center_free(v_current_memory);
            } else {
                notify_center_full(v_current_memory);
            }
        }

        void notify_center_full(const size_t p_v_current_memory) {
            // last iter, center wasn't full but now it is => warn nodes to stop sending
            if (p_v_current_memory > MAX_MEMORY_MB || m_center_bundle_queue.size() > CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size) {
                for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                    MPI_Send(nullptr, 0, MPI_BYTE, v_rank, CENTER_IS_FULL, m_center_fullness_communicator);
                }
                ++m_fullness_notices_sent;
                m_center_last_full_status = true;
                m_time_centerfull_sent = MPI_Wtime();
                if (auto* v_telemetry = telemetry::get()) {
                    v_telemetry->record_center_full();
                }

                // cout << "CENTER IS FULL" << endl;
            }
        }

        void notify_center_free(const size_t p_v_current_memory) {
            // last iter, center was full but now it has space => warn others it's ok
            if (static_cast<double>(p_v_current_memory) <= 0.9 * MAX_MEMORY_MB &&
                static_cast<double>(m_center_bundle_queue.size()) < CENTER_NBSTORED_TASKS_PER_PROCESS * m_world_size * 0.8) {
                for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                    MPI_Send(nullptr, 0, MPI_BYTE, v_rank, CENTER_IS_FREE, m_center_fullness_communicator);
                }
                ++m_fullness_notices_sent;
                m_center_last_full_status = false;
                if (auto* v_telemetry = telemetry::get()) {
                    v_telemetry->record_center_free();
                }

                // cout << "CENTER IS NOT FULL ANYMORE" << endl;
            }
        }

        void notify_termination() const {
            const std::array<int, 2> v_sent_counts = {m_score_updates_sent, m_fullness_notices_sent};
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                MPI_Send(v_sent_counts.data(), static_cast<int>(v_sent_counts.size()), MPI_INT, v_rank, TERMINATION, m_world_communicator);
            }
            MPI_Barrier(m_world_communicator);
        }

        int get_available() {
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                if (m_process_state[v_rank] == AVAILABLE_STATE)
                    return v_rank;
            }
            return -1; // all nodes are running
        }

        /*	receive solution from nodes */
        void receive_solution() {
            for (int v_rank = 1; v_rank < m_world_size; v_rank++) {
                receive_solution_from(v_rank);
            }
        }

        void receive_solution_from(const int p_rank) {
            while (true) {
                MPI_Status v_status;
                int v_count;
                // sender would not need to send data size before hand **********************************************
                MPI_Probe(p_rank, MPI_ANY_TAG, m_world_communicator, &v_status); // receives status before receiving the message
                MPI_Get_count(&v_status, MPI_BYTE, &v_count); // receives total number of datatype elements of the message
                //***************************************************************************************************

                switch (v_status.MPI_TAG) {
                    case RUNNING_STATE:
                    case AVAILABLE_STATE: {
                        consume_int_flag(v_status, m_world_communicator);
                        m_total_requests_number++;
                        m_stats.m_total_requested_tasks++;
                        m_stats.m_center_messages_handled++;
                        if (auto* v_telemetry = telemetry::get()) {
                            v_telemetry->record_center_state_msg();
                        }
                        break;
                    }
                    case HAS_RESULT: {
                        task_packet v_task_packet(v_count);
                        MPI_Recv(v_task_packet.data(), v_count, MPI_BYTE, p_rank, HAS_RESULT, m_world_communicator, &v_status);

                        score v_score{};
                        MPI_Recv(&v_score, sizeof(score), MPI_BYTE, p_rank, HAS_RESULT, m_world_communicator, &v_status);

                        m_best_results[p_rank] = result{v_score, v_task_packet};
                        spdlog::debug("solution received from rank {}, count : {}, global score {} \n", p_rank, v_count, v_score.to_string());
                        return;
                    }
                    case NO_RESULT: {
                        // consumes the empty message
                        MPI_Recv(nullptr, 0, MPI_BYTE, p_rank, NO_RESULT, m_world_communicator, &v_status);
                        spdlog::debug("solution NOT received from rank {}\n", p_rank);
                        return;
                    }
                    default: {
                        utils::log_and_throw("rank {} received unknown tag {} from rank {} in receive_solution()", m_world_rank, v_status.MPI_TAG, p_rank);
                    };
                }
            }
        }

        void send_seed(task_packet& p_packet) {
            constexpr int DEST = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[DEST] = RUNNING_STATE;
            // *********************************************

            int v_err = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, DEST, TASK_FROM_CENTER, m_world_communicator); // send buffer
            if (v_err != MPI_SUCCESS)
                spdlog::debug("buffer failed to send! \n");

            spdlog::debug("Seed sent \n");
            m_sent_tasks++;
            m_total_requests_number++;
            m_stats.m_sent_task_count++;
            m_stats.m_total_requested_tasks++;
        }

        void send_seed(const task_packet& p_packet, const int p_runnable_id) {
            constexpr int DEST = 1;
            // global synchronisation **********************
            --m_nodes_available;
            m_process_state[DEST] = RUNNING_STATE;
            // *********************************************

            m_termination.on_task_sent();
            const auto v_send_start = std::chrono::steady_clock::now();
            const int v_err1 = MPI_Ssend(p_packet.data(), static_cast<int>(p_packet.size()), MPI_BYTE, DEST, TASK_FROM_CENTER, m_world_communicator); // send buffer
            if (v_err1 != MPI_SUCCESS) {
                spdlog::debug("buffer failed to send! \n");
            }
            const int v_err2 = MPI_Ssend(&p_runnable_id, 1, MPI_INT, DEST, RUNNABLE_ID, m_world_communicator);
            if (v_err2 != MPI_SUCCESS) {
                spdlog::debug("runnable id failed to send! \n");
            }
            const auto v_send_elapsed = std::chrono::steady_clock::now() - v_send_start;

            spdlog::debug("Seed sent \n");
            m_sent_tasks++;
            m_total_requests_number++;
            m_stats.m_sent_task_count++;
            m_stats.m_total_requested_tasks++;
            m_stats.m_center_messages_handled++;

            if (auto* v_telemetry = telemetry::get()) {
                v_telemetry->record_send(static_cast<std::uint32_t>(DEST), p_packet.size(), v_send_elapsed);
                v_telemetry->record_assignment(static_cast<std::uint32_t>(DEST));
            }
        }

    private:
        int m_received_tasks = 0;
        int m_sent_tasks = 0;
        size_t m_total_requests_number = 0;
        double m_start_time = 0;
        double m_end_time = 0;
        double m_idle_time = 0;

        int m_world_rank{}; // get the rank of the process
        int m_world_size{}; // get the number of processes/nodes
        char m_processor_name[MPI_MAX_PROCESSOR_NAME]{}; // name of the node

        int m_nodes_running = 0;
        int m_nodes_available = 0;
        int m_score_updates_sent = 0;
        int m_fullness_notices_sent = 0;
        int m_score_updates_received = 0;
        int m_fullness_notices_received = 0;
        std::vector<int> m_process_state; // state of the nodes: running, assigned or available
        bool m_custom_initial_topology = false; // true if the user has set a custom topology
        tree m_process_tree;

        std::mutex m_mutex;
        std::atomic<bool> m_transmitting;
        int m_destination_rank = -1;

        queue<task_packet*> m_tasks_queue;
        queue<std::shared_ptr<task_bundle>> m_tasks_bundle_queue;

        MPI_Comm m_global_score_communicator{}; // BIDIRECTIONAL
        MPI_Comm m_center_fullness_communicator{}; // CENTER TO WORKER (ONLY)
        MPI_Comm m_world_communicator{}; // BIDIRECTIONAL

        score m_global_score{};

        bool m_is_center_full = false;

        goal m_goal = MAXIMISE;

        std::vector<result> m_best_results;

        size_t m_threads_per_process = std::thread::hardware_concurrency(); // detects the number of logical processors in machine

        termination_detector m_termination{false};
        default_mpi_stats m_stats{-1};
        std::vector<default_mpi_stats> m_stats_vector;

        void init_mpi() {
            set_thread_support_level();
            create_mpi_communicators();

            int v_name_length;
            MPI_Get_processor_name(m_processor_name, &v_name_length);
            spdlog::debug("Process {} of {} is on {}\n", m_world_rank, m_world_size, m_processor_name);
        }

        void set_thread_support_level() {
            // Initialize MPI and ask for thread support
            int* v_argc = nullptr;
            char** v_argv = nullptr;
            int v_provided;
            MPI_Init_thread(v_argc, &v_argv, MPI_THREAD_FUNNELED, &v_provided);

            if (v_provided < MPI_THREAD_FUNNELED) {
                spdlog::debug("The threading support level is lesser than that demanded.\n");
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
                spdlog::debug("At least two processes required !!\n");
                MPI_Abort(m_world_communicator, EXIT_FAILURE);
            }
#endif
        }

        void init_member_variables() {
            m_termination = termination_detector(m_world_rank == CENTER_NODE);
            m_process_state.resize(m_world_size, AVAILABLE_STATE);
            m_nodes_available = m_world_size - 1;
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

        static bool should_update_global(const goal p_goal, const score& p_global_score, const score& p_local_score) { return should_update(p_goal, p_global_score, p_local_score); }

        static bool should_update_local(const goal p_goal, const score& p_global_score, const score& p_local_score) { return should_update(p_goal, p_local_score, p_global_score); }

        static bool should_broadcast_global(const goal p_goal, const score& p_old_global, const score& p_new_global) { return should_update(p_goal, p_old_global, p_new_global); }

        static bool should_update(const goal p_goal, const score& p_old_value, const score& p_new_value) {
            const bool v_new_max_is_better = p_goal == MAXIMISE && p_new_value > p_old_value;
            const bool v_new_min_is_better = p_goal == MINIMISE && p_new_value < p_old_value;
            return v_new_max_is_better || v_new_min_is_better;
        }


        class center_view final : public center {

            mpi_centralized_scheduler& m_parent;

        public:
            explicit center_view(mpi_centralized_scheduler& p_parent) : m_parent(p_parent) {}

            ~center_view() override = default;

            void barrier() override { m_parent.barrier(); }

            [[nodiscard]] int rank_me() const override { return m_parent.rank_me(); }

            [[nodiscard]] int world_size() const override { return m_parent.world_size(); }

            [[nodiscard]] std::unique_ptr<stats> get_stats() const override { return m_parent.get_stats(); }

            void run(const task_packet p_task, const int p_runnable_id) override { m_parent.run(p_task, p_runnable_id); }

            task_packet get_result() override { return m_parent.fetch_solution(); }

            std::vector<result> get_all_results() override { return m_parent.fetch_result_vector(); }
        };

        class worker_view final : public worker {
            mpi_centralized_scheduler& m_parent;

        public:
            explicit worker_view(mpi_centralized_scheduler& p_parent) : m_parent(p_parent) {}

            ~worker_view() override = default;

            void barrier() override { m_parent.barrier(); }

            [[nodiscard]] int rank_me() const override { return m_parent.rank_me(); }

            [[nodiscard]] int world_size() const override { return m_parent.world_size(); }

            [[nodiscard]] std::unique_ptr<stats> get_stats() const override { return m_parent.get_stats(); }

            void run(node_manager& p_node_manager, std::map<int, std::shared_ptr<serial_runnable>> p_runnables) override { m_parent.run(p_node_manager, std::move(p_runnables)); }

            unsigned int force_push(task_packet&& p_task, const int p_function_id) override { return m_parent.force_push(std::move(p_task), p_function_id); }

            [[nodiscard]] unsigned int next_process() const override { return 0; }

            std::optional<transmission_guard> try_open_transmission_channel() override { return m_parent.try_open_transmission_channel(); }
        };

        // scheduler views
        center_view m_center_view{*this};
        worker_view m_worker_view{*this};

    public:
        // ————————— ↓↓↓↓  New development ↓↓↓↓ ——————————
        center& center_view() override {
            if (m_world_rank != 0) {
                utils::log_and_throw("Only rank 0 can access center_view()");
            }
            return m_center_view;
        }

        worker& worker_view() override {
            if (m_world_rank == 0) {
                utils::log_and_throw("Rank 0 cannot access worker_view()");
            }
            return m_worker_view;
        }
    };

} // namespace gempba

#endif
