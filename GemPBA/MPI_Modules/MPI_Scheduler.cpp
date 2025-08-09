#include <MPI_Modules/MPI_Scheduler.hpp>
#include <BranchHandler/BranchHandler.hpp>

namespace gempba {
    void MPI_Scheduler::taskFunneling(BranchHandler &branchHandler) {
        task_packet *v_packet = nullptr;
        bool isPop = m_tasks_queue.pop(v_packet);
        // nice(18); // this method changes OS priority of current thread, it should be carefully used

        while (true) {
            while (isPop) {
                // as long as there is a message

                std::scoped_lock<std::mutex> lck(m_mutex);
                std::unique_ptr<task_packet> ptr(v_packet);
                m_sent_tasks++;

                sendTask(*v_packet);

                isPop = m_tasks_queue.pop(v_packet);

                if (!isPop) {
                    m_transmitting = false;
                } else {
                    delete v_packet;
                    throw std::runtime_error("Task found in queue, this should not happen in taskFunneling()\n");
                }
            } {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> lck(m_mutex);
                maybe_receive_reference_value();
                maybe_receive_next_process();

                updateRefValue(branchHandler);
                updateNextProcess();
            }

            isPop = m_tasks_queue.pop(v_packet);

            if (!isPop && branchHandler.isDone()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                isPop = m_tasks_queue.pop(v_packet);

                if (!isPop) {
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

    void MPI_Scheduler::updateRefValue(BranchHandler &branchHandler) {
        int _refGlobal = m_global_reference_value; // constant within this scope
        int _refLocal = branchHandler.refValue(); // constant within this scope

        // static size_t C = 0;

        if ((m_maximisation && _refGlobal > _refLocal) || (!m_maximisation && _refGlobal < _refLocal)) {
            branchHandler.updateRefValue(_refGlobal);
        } else if ((m_maximisation && _refLocal > _refGlobal) || (!m_maximisation && _refLocal < _refGlobal)) {
            MPI_Ssend(&_refLocal, 1, MPI_INT, CENTER_NODE, REFERENCE_VAL_PROPOSAL, m_global_reference_value_communicator);
        }
    }
}
