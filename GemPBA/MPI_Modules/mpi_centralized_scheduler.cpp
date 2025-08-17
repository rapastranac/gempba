#include "mpi_centralized_scheduler.hpp"
#include "BranchHandler/branch_handler.hpp"

namespace gempba {
    void mpi_centralized_scheduler::task_funneling(branch_handler &p_branch_handler) {
        task_packet *v_message = nullptr;
        bool v_is_pop = m_task_queue.pop(v_message);

        while (true) {
            while (v_is_pop) // as long as there is a message
            {
                std::scoped_lock<std::mutex> v_lock(m_mutex);

                std::unique_ptr<task_packet> v_pointer(v_message);
                m_sent_tasks++;

                send_task_to_center(*v_message);

                v_is_pop = m_task_queue.pop(v_message);

                if (!v_is_pop)
                    m_transmitting = false;
                else {
                    throw std::runtime_error("Task found in queue, this should not happen in taskFunneling()\n");
                }
            } {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> v_lock(m_mutex);
                maybe_receive_reference_value_from_center();
                maybe_receive_center_fullness();

                update_ref_value(p_branch_handler);
            }

            v_is_pop = m_task_queue.pop(v_message);

            if (!v_is_pop && p_branch_handler.is_done()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_task_queue.pop(v_message);

                if (!v_is_pop)
                    break;
            }
        }
        #ifdef GEMPBA_DEBUG_COMMENTS
        spdlog::debug("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);
        #endif

        if (!m_task_queue.empty())
            throw std::runtime_error("leaving process with a pending message\n");
        /* to reuse the task funneling, otherwise it will exit
        right away the second time the process receives a task*/

        // nice(0);
    }

    void mpi_centralized_scheduler::update_ref_value(branch_handler &p_branch_handler) {
        const score v_reference_global = m_global_score; // constant within this scope
        const score v_reference_local = score::make(p_branch_handler.get_score()); // constant within this scope

        if (should_update_local(m_goal, v_reference_global, v_reference_local)) {
            p_branch_handler.try_update_reference_value_and_invalidate_result(v_reference_global.get_loose<int>());
        } else if (should_update_global(m_goal, v_reference_global, v_reference_local)) {
            MPI_Ssend(&v_reference_local, sizeof(score), MPI_BYTE, CENTER, REFVAL_PROPOSAL_TAG, m_global_reference_value_communicator);
        }
    }
}
