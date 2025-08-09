#include "mpi_centralized_scheduler.hpp"
#include "BranchHandler/BranchHandler.hpp"

namespace gempba {
    void mpi_centralized_scheduler::task_funneling(BranchHandler &p_branch_handler) {
        task_packet *v_message = nullptr;
        bool v_is_pop = m_task_queue.pop(v_message);

        while (true) {
            while (v_is_pop) // as long as there is a message
            {
                std::scoped_lock<std::mutex> v_lock(m_mutex);

                std::unique_ptr<task_packet> v_pointer(v_message);
                m_number_tasks_sent++;

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
                probe_reference_value();
                probe_center_request();

                update_ref_value(p_branch_handler);
            }

            v_is_pop = m_task_queue.pop(v_message);

            if (!v_is_pop && p_branch_handler.isDone()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_task_queue.pop(v_message);

                if (!v_is_pop)
                    break;
            }
        }
        #ifdef GEMPBA_DEBUG_COMMENTS
        spdlog::debug("rank {} sent {} tasks\n", m_world_rank, m_number_tasks_sent);
        #endif

        if (!m_task_queue.empty())
            throw std::runtime_error("leaving process with a pending message\n");
        /* to reuse the task funneling, otherwise it will exit
        right away the second time the process receives a task*/

        // nice(0);
    }

    void mpi_centralized_scheduler::update_ref_value(BranchHandler &p_branch_handler) {
        const int v_reference_global = m_ref_value_global; // constant within this scope
        const int v_reference_local = p_branch_handler.refValue(); // constant within this scope

        // static size_t C = 0;

        if ((m_maximisation && v_reference_global > v_reference_local) || (!m_maximisation && v_reference_global < v_reference_local)) {
            p_branch_handler.updateRefValue(v_reference_global);
        } else if ((m_maximisation && v_reference_local > v_reference_global) || (!m_maximisation && v_reference_local < v_reference_global)) {
            MPI_Ssend(&v_reference_local, 1, MPI_INT, 0, REFVAL_UPDATE_TAG, m_world_comm);
        }
    }
}
