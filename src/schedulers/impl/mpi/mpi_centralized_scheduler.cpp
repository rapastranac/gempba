#include <branch_handling/branch_handler.hpp>
#include <schedulers/impl/mpi/mpi_centralized_scheduler.hpp>

namespace gempba {
    void mpi_centralized_scheduler::collect_stats_data(const branch_handler &p_branch_handler) {
        m_idle_time = p_branch_handler.get_pool_idle_time();
        m_stats.m_idle_time = p_branch_handler.get_pool_idle_time();
        m_stats.m_total_thread_requests = p_branch_handler.number_thread_requests();
    }

    void mpi_centralized_scheduler::task_funneling(branch_handler &p_branch_handler) {
        task_packet *v_packet = nullptr;
        bool v_is_pop = m_tasks_queue.pop(v_packet);

        while (true) {
            while (v_is_pop) {
                // as long as there is a message

                std::scoped_lock v_lock(m_mutex);
                std::unique_ptr<task_packet> v_unique_pointer(v_packet);
                m_sent_tasks++;
                m_total_requests_number++;
                m_stats.m_sent_task_count++;
                m_stats.m_total_requested_tasks++;


                send_task_to_center(*v_packet);

                v_is_pop = m_tasks_queue.pop(v_packet);

                if (!v_is_pop) {
                    m_transmitting = false;
                } else {
                    // Let the unique_ptr handle cleanup automatically
                    std::unique_ptr<task_packet> error_packet(v_packet);
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

            v_is_pop = m_tasks_queue.pop(v_packet);

            if (!v_is_pop && p_branch_handler.is_done()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_tasks_queue.pop(v_packet);

                if (!v_is_pop) {
                    break;
                }
            }
        }
        utils::print_ipc_debug_comments("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);

        if (!m_tasks_queue.empty()) {
            spdlog::throw_spdlog_ex("leaving process with a pending message\n");
        }
        /* to reuse the task funneling, otherwise it will exit
        right away the second time the process receives a task*/

        // nice(0);
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

    void mpi_centralized_scheduler::send_solution(branch_handler &p_branch_handler) const {
        const std::optional<result> &v_result_bytes_opt = p_branch_handler.get_result_bytes();
        if (!v_result_bytes_opt.has_value()) {
            MPI_Ssend(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            return;
        }

        const result v_result = v_result_bytes_opt.value();
        const score v_score = v_result.get_score();
        task_packet v_task_packet = v_result.get_task_packet();

        if (v_task_packet == task_packet::EMPTY) {
            MPI_Ssend(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
        } else {
            MPI_Ssend(v_task_packet.data(), static_cast<int>(v_task_packet.size()), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
            MPI_Ssend(&v_score, sizeof(score), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
        }
    }
}
