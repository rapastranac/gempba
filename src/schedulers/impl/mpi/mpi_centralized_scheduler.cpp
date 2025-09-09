#include <branch_handling/branch_handler.hpp>
#include <schedulers/impl/mpi/mpi_centralized_scheduler.hpp>

namespace gempba {
    void mpi_centralized_scheduler::task_funneling(branch_handler &p_branch_handler) {
        task_packet *v_message = nullptr;
        bool v_is_pop = m_tasks_queue.pop(v_message);

        while (true) {
            while (v_is_pop) // as long as there is a message
            {
                std::scoped_lock<std::mutex> v_lock(m_mutex);

                std::unique_ptr<task_packet> v_pointer(v_message);
                m_sent_tasks++;

                send_task_to_center(*v_message);

                v_is_pop = m_tasks_queue.pop(v_message);

                if (!v_is_pop)
                    m_transmitting = false;
                else {
                    throw std::runtime_error("Task found in queue, this should not happen in taskFunneling()\n");
                }
            } {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> v_lock(m_mutex);
                maybe_receive_score_from_center();
                maybe_receive_center_fullness();

                update_score(p_branch_handler);
            }

            v_is_pop = m_tasks_queue.pop(v_message);

            if (!v_is_pop && p_branch_handler.is_done()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_tasks_queue.pop(v_message);

                if (!v_is_pop)
                    break;
            }
        }
        #if GEMPBA_DEBUG_COMMENTS
        spdlog::debug("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);
        #endif

        if (!m_tasks_queue.empty())
            throw std::runtime_error("leaving process with a pending message\n");
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
}
