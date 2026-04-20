#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <functional>
#include <gempba/core/scheduler_traits.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/utils/gempba_utils.hpp>
#include <gempba/utils/result.hpp>
#include <gempba/utils/score.hpp>
#include <gempba/utils/task_packet.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/tree.hpp>
#include <map>
#include <memory>

namespace gempba {

    class result_holder_parent;

    class node_manager;

    class scheduler : public scheduler_traits {
    protected:
        scheduler() = default;

    public:
        ~scheduler() override = default;

        [[nodiscard]] virtual double elapsed_time() const = 0;

        virtual void set_goal(goal p_goal, score_type p_type) = 0;

        virtual void set_custom_initial_topology(tree &&p_tree) = 0;

        /**
         * Get the statistics of all processes as a vector of unique pointers. This is useful for gathering stats from all processes.
         *
         * @attention this method should be called only after synchronize_stats() to ensure that the stats are up-to-date, and should be called only by the root process (rank 0).
         * @return A vector of unique pointers to the stats objects of all processes.
         */
        [[nodiscard]] virtual std::vector<std::unique_ptr<stats>> get_stats_vector() const = 0;

        /**
         * Synchronize the statistics across all processes. This method should be called to ensure that the stats are up-to-date.
         *
         * @attention This function should be called by all processes.
         */
        virtual void synchronize_stats() = 0;

        // ————————— ↓↓↓↓  New development ↓↓↓↓ ——————————

        class center : public scheduler_traits {
        protected:
            center() = default;

        public:
            ~center() override = default;

            virtual void run(task_packet p_task, int p_runnable_id) = 0;

            /**
             *
             * @return the best result found across all worker nodes
             */
            virtual task_packet get_result() = 0;

            /**
             * Each index of the returned vector corresponds to the rank of the worker node. If no result was found by a worker node, the corresponding index will contain an empty result.
             *
             * Also, for rank 0 (the center node), the corresponding index will contain an empty result, as the center node does not perform any computations.
             *
             * @return  all results found across all worker nodes
             */
            virtual std::vector<result> get_all_results() = 0;
        };

        class worker : public scheduler_traits {
        protected:
            worker() = default;

        public:
            ~worker() override = default;

            virtual void run(node_manager &p_node_manager, std::map<int, std::shared_ptr<serial_runnable>> p_runnables) = 0;

            virtual unsigned int force_push(task_packet &&p_task, int p_function_id) = 0;


            [[nodiscard]] virtual unsigned int next_process() const = 0;

            /**
             * @brief Attempts to open the transmission channel.
             *
             * This method attempts to open the transmission channel. The implementation should
             * handle any necessary initialization or acquisition of resources, including mutexes
             * or other synchronization mechanisms.
             *
             *
             * @warning The method should be implemented to handle potential race conditions and
             *          ensure that the resource is properly managed, especially if multiple threads
             *          might interact with the transmission channel concurrently.
             */
            virtual std::optional<transmission_guard> try_open_transmission_channel() = 0;
        };

        virtual center &center_view() = 0;

        virtual worker &worker_view() = 0;
    };
} // namespace gempba

#endif // GEMPBA_SCHEDULER_HPP
