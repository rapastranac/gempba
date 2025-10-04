#include <branch_handling/branch_handler.hpp>
#include <schedulers/impl/mpi/mpi_centralized_scheduler.hpp>

namespace gempba {
    void mpi_centralized_scheduler::send_final_solution_to_center(branch_handler &p_branch_handler) const {
        auto v_bytes_opt = p_branch_handler.get_result_bytes();

        if (!v_bytes_opt.has_value()) {
            MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            return;
        }

        const result result = v_bytes_opt.value();
        const task_packet bytes = result.get_task_packet();

        if (bytes.size() == 0) {
            MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            return;
        }

        MPI_Send(bytes.data(), static_cast<int>(bytes.size()), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);

        const score &v_score = result.get_score();
        MPI_Send(&v_score, sizeof(score), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
    }

    void mpi_centralized_scheduler::collect_stats_data(const branch_handler &p_branch_handler) {
        m_idle_time = p_branch_handler.get_idle_time();
        m_stats.m_idle_time = p_branch_handler.get_idle_time();
        m_stats.m_total_thread_requests = p_branch_handler.get_thread_request_count();
    }

    void mpi_centralized_scheduler::task_funneling(branch_handler &p_branch_handler) {
        std::shared_ptr<task_bundle> v_packet;
        bool v_is_pop = m_tasks_bundle_queue.pop(v_packet);

        while (true) {
            while (v_is_pop) {
                // as long as there is a message
                std::scoped_lock v_lock(m_mutex);
                m_sent_tasks++;
                m_total_requests_number++;
                m_stats.m_sent_task_count++;
                m_stats.m_total_requested_tasks++;


                send_task_to_center(*v_packet);

                v_is_pop = m_tasks_bundle_queue.pop(v_packet);

                if (!v_is_pop) {
                    m_transmitting = false;
                } else {
                    spdlog::throw_spdlog_ex("Task found in queue, this should not happen in task_funneling()\n");
                }
            }
            {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> v_lock(m_mutex);
                maybe_receive_score_from_center();
                maybe_receive_center_fullness();

                update_score(p_branch_handler);
            }

            v_is_pop = m_tasks_bundle_queue.pop(v_packet);

            if (!v_is_pop && p_branch_handler.is_done()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_tasks_bundle_queue.pop(v_packet);

                if (!v_is_pop) {
                    break;
                }
            }
        }
        utils::print_ipc_debug_comments("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);

        if (!m_tasks_bundle_queue.empty()) {
            spdlog::throw_spdlog_ex("leaving process with a pending message\n");
        }
    }

    void mpi_centralized_scheduler::update_score(branch_handler &p_branch_handler) {
        const score v_global_score_temp = m_global_score; // constant within this scope
        const score v_local_score_temp = p_branch_handler.get_score(); // constant within this scope

        if (should_update_local(m_goal, v_global_score_temp, v_local_score_temp)) {
            p_branch_handler.try_update_score_and_invalidate_result(v_global_score_temp);
        } else if (should_update_global(m_goal, v_global_score_temp, v_local_score_temp)) {
            MPI_Ssend(&v_local_score_temp, sizeof(score), MPI_BYTE, CENTER_NODE, SCORE_PROPOSAL, m_global_score_communicator);
        }
    }

}
