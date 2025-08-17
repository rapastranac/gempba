#include <MPI_Modules/mpi_scheduler.hpp>
#include <BranchHandler/branch_handler.hpp>

namespace gempba {
    void mpi_scheduler::task_funneling(branch_handler &p_branch_handler) {
        task_packet *v_packet = nullptr;
        bool v_is_pop = m_tasks_queue.pop(v_packet);
        // nice(18); // this method changes OS priority of current thread, it should be carefully used

        while (true) {
            while (v_is_pop) {
                // as long as there is a message

                std::scoped_lock<std::mutex> v_lock(m_mutex);
                std::unique_ptr<task_packet> v_unique_pointer(v_packet);
                m_sent_tasks++;

                send_task(*v_packet);

                v_is_pop = m_tasks_queue.pop(v_packet);

                if (!v_is_pop) {
                    m_transmitting = false;
                } else {
                    delete v_packet;
                    throw std::runtime_error("Task found in queue, this should not happen in taskFunneling()\n");
                }
            } {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> v_lock(m_mutex);
                maybe_receive_reference_value();
                maybe_receive_next_process();

                update_ref_value(p_branch_handler);
                update_next_process();
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
        utils::print_mpi_debug_comments("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);

        if (!m_tasks_queue.empty()) {
            throw std::runtime_error("leaving process with a pending message\n");
        }
        /* to reuse the task funneling, otherwise it will exit
        right away the second time the process receives a task*/

        // nice(0);
    }

    void mpi_scheduler::update_ref_value(branch_handler &p_branch_handler) {
        const int v_reference_global = m_global_reference_value; // constant within this scope
        const int v_reference_local = p_branch_handler.reference_value(); // constant within this scope

        if (should_update_local(m_goal, v_reference_global, v_reference_local)) {
            p_branch_handler.try_update_reference_value_and_invalidate_result(v_reference_global);
        } else if (should_update_global(m_goal, v_reference_global, v_reference_local)) {
            MPI_Ssend(&v_reference_local, 1, MPI_INT, CENTER_NODE, REFERENCE_VAL_PROPOSAL, m_global_reference_value_communicator);
        }
    }
}
