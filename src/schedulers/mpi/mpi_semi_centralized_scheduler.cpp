#include <gempba/node_manager.hpp>
#include <impl/schedulers/mpi_semi_centralized_scheduler.hpp>

namespace gempba {
    void mpi_semi_centralized_scheduler::send_final_solution_to_center(node_manager &p_node_manager) const {
        auto v_bytes_opt = p_node_manager.get_result_bytes();

        if (!v_bytes_opt.has_value()) {
            MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            return;
        }

        const result &v_result = v_bytes_opt.value();
        const task_packet v_bytes = v_result.get_task_packet();

        if (v_bytes.empty()) {
            MPI_Send(nullptr, 0, MPI_BYTE, CENTER_NODE, NO_RESULT, m_world_communicator);
            return;
        }

        MPI_Send(v_bytes.data(), static_cast<int>(v_bytes.size()), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);

        const score &v_score = v_result.get_score();
        MPI_Send(&v_score, sizeof(score), MPI_BYTE, CENTER_NODE, HAS_RESULT, m_world_communicator);
    }

    void mpi_semi_centralized_scheduler::collect_stats_data(const node_manager &p_node_manager) {
        m_idle_time = p_node_manager.get_idle_time();
        m_stats.m_idle_time = p_node_manager.get_idle_time();
        m_stats.m_total_thread_requests = p_node_manager.get_thread_request_count();
    }

    void mpi_semi_centralized_scheduler::task_funneling(node_manager &p_node_manager) {
        std::shared_ptr<task_bundle> v_bundle;
        bool v_is_pop = m_tasks_bundle_queue.pop(v_bundle);

        while (true) {
            while (v_is_pop) {
                // as long as there is a message

                std::scoped_lock v_lock(m_mutex);
                m_sent_tasks++;
                m_total_requests_number++;
                m_stats.m_sent_task_count++;
                m_stats.m_total_requested_tasks++;

                send_task(*v_bundle);

                v_is_pop = m_tasks_bundle_queue.pop(v_bundle);

                if (!v_is_pop) {
                    m_transmitting = false;
                } else {
                    utils::log_and_throw("Task found in queue, this should not happen in task_funneling()\n");
                }
            }
            {
                /* this section protects MPI calls */
                std::scoped_lock v_lock(m_mutex);
                maybe_receive_score_from_center();
                maybe_receive_next_process();

                update_score(p_node_manager);
                update_next_process();
            }

            v_is_pop = m_tasks_bundle_queue.pop(v_bundle);

            if (!v_is_pop && p_node_manager.is_done()) {
                /* by the time this thread realises that the thread pool has no more tasks,
                    another buffer might have been pushed, which should be verified in the next line*/
                v_is_pop = m_tasks_bundle_queue.pop(v_bundle);

                if (!v_is_pop) {
                    break;
                }
            }
        }
        // This call here is thread safe because all tasks have been resolved and no other thread is pushing new tasks
        update_score(p_node_manager); // Attempt final score update before exiting
        utils::print_ipc_debug_comments("rank {} sent {} tasks\n", m_world_rank, m_sent_tasks);

        if (!m_tasks_bundle_queue.empty()) {
            utils::log_and_throw("leaving process with a pending message\n");
        }
    }

    void mpi_semi_centralized_scheduler::update_score(node_manager &p_node_manager) {
        const score v_global_score_temp = m_global_score; // constant within this scope
        const score v_local_score_temp = p_node_manager.get_score(); // constant within this scope

        if (should_update_local(m_goal, v_global_score_temp, v_local_score_temp)) {
            p_node_manager.try_update_score_and_invalidate_result(v_global_score_temp);
        } else if (should_update_global(m_goal, v_global_score_temp, v_local_score_temp)) {
            MPI_Ssend(&v_local_score_temp, sizeof(score), MPI_BYTE, CENTER_NODE, SCORE_PROPOSAL, m_global_score_communicator);
        }
    }

} // namespace gempba
