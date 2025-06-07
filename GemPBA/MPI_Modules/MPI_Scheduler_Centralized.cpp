#include "MPI_Scheduler_Centralized.hpp"
#include "BranchHandler/BranchHandler.hpp"

namespace gempba {
    void MPI_SchedulerCentralized::taskFunneling(BranchHandler& branchHandler) {
        std::string* message = nullptr;
        bool isPop = q.pop(message);

        while (true) {
            while (isPop) // as long as there is a message
            {
                std::scoped_lock<std::mutex> lck(mtx);

                std::unique_ptr<std::string> ptr(message);
                nTasksSent++;

                sendTaskToCenter(*message);

                isPop = q.pop(message);

                if (!isPop)
                    transmitting = false;
                else {
                    throw std::runtime_error("Task found in queue, this should not happen in taskFunneling()\n");
                }
            }
            {
                /* this section protects MPI calls */
                std::scoped_lock<std::mutex> lck(mtx);
                probe_refValue();
                probe_centerRequest();

                updateRefValue(branchHandler);
            }

            isPop = q.pop(message);

            if (!isPop && branchHandler.isDone()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                isPop = q.pop(message);

                if (!isPop)
                    break;
            }
        }
        #ifdef DEBUG_COMMENTS
        spdlog::debug("rank {} sent {} tasks\n", world_rank, nTasksSent);
        #endif

        if (!q.empty())
            throw std::runtime_error("leaving process with a pending message\n");
        /* to reuse the task funneling, otherwise it will exit
        right away the second time the process receives a task*/

        // nice(0);
    }

    void MPI_SchedulerCentralized::updateRefValue(BranchHandler& branchHandler) {
        int _refGlobal = refValueGlobal; // constant within this scope
        int _refLocal = branchHandler.refValue(); // constant within this scope

        // static size_t C = 0;

        if ((maximisation && _refGlobal > _refLocal) || (!maximisation && _refGlobal < _refLocal)) {
            branchHandler.updateRefValue(_refGlobal);
        } else if ((maximisation && _refLocal > _refGlobal) || (!maximisation && _refLocal < _refGlobal)) {
            MPI_Ssend(&_refLocal, 1, MPI_INT, 0, REFVAL_UPDATE_TAG, world_Comm);
        }
    }
}
