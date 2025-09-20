#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <functional>
#include <memory>
#include <schedulers/api/scheduler_traits.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/tree.hpp>
#include <utils/ipc/result.hpp>
#include <utils/ipc/score.hpp>
#include <utils/ipc/task_packet.hpp>

namespace gempba {

    class result_holder_parent;

    class branch_handler;

    class scheduler : public scheduler_traits {
    public:
        scheduler() = default;

        ~scheduler() override = default;

        virtual task_packet fetch_solution() = 0;

        virtual std::vector<result> fetch_result_vector() = 0;

        [[nodiscard]] virtual double elapsed_time() const = 0;

        virtual bool try_open_transmission_channel() = 0;

        virtual void close_transmission_channel() = 0;

        [[nodiscard]] virtual int next_process() const = 0;

        virtual void set_goal(goal p_goal, score_type p_type) = 0;

        virtual void push(task_packet &&p_task_packet) = 0;

        virtual void run_node(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                              std::function<result()> &p_result_fetcher) = 0;

        virtual void run_center(task_packet &p_seed) = 0;

        virtual void set_custom_initial_topology(tree &&p_tree) = 0;

        /**
         * Get the statistics of all processes as a vector of unique pointers. This is useful for gathering stats from all processes.
         *
         * @attention this method should be called only after synchronize_stats() to ensure that the stats are up-to-date, and should be called only by the root process (rank 0).
         * @return A vector of unique pointers to the stats objects of all processes.
         */
        [[nodiscard]] virtual std::vector<std::unique_ptr<stats> > get_stats_vector() const = 0;

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

            /**
             * Runs the scheduler for the center node
             *
             * @param p_task task to be broadcast to the first available worker node
             */
            virtual void run(task_packet p_task) = 0;

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

            /**
            * Runs the scheduler for the worker node
            *
            * @param p_branch_handler Reference to the branch handler that manages task distribution and result collection.
            * @param p_buffer_decoder Function to decode incoming task packets into result holders.
            * @param p_result_fetcher Function to fetch results after task execution.
            */
            virtual void run(branch_handler &p_branch_handler, std::function<std::shared_ptr<result_holder_parent>(task_packet)> &p_buffer_decoder,
                             std::function<result()> &p_result_fetcher) = 0;

            /**
            * Pushes a message to the next assigned process. This method is not thread-safe. Sending channel must be opened before calling this
            * and closed after calling it. Use try_open_transmission_channel() and close_transmission_channel() to manage the sending channel.
            * Use judiciously. So far, this is only called within the node_manager. It is public just to avoid making the node_manager a friend class.
            *
            * @param p_task The serialized message to be sent.
            */
            virtual void push(task_packet &&p_task) = 0;

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
            virtual bool try_open_transmission_channel() = 0;

            /**
             * @brief Closes the transmission channel. WIP - perhaps it can be removed
             *
             * This method is used to close the transmission channel. The implementation must
             * ensure that any resources associated with the channel, including mutexes, are
             * released or unlocked as appropriate.
             *
             * @contract The method must adhere to the following contract:
             * - If a mutex or other synchronization mechanism is used, it should be unlocked
             *   only if it is currently owned by the thread calling this method.
             * - The method must not leave the channel in an inconsistent state.
             *
             * @warning Attempting to unlock a mutex that is not owned by the current thread, or
             * performing other unsafe operations, may lead to undefined behavior. Ensure
             * that the implementation follows proper synchronization practices.
             *
             */
            virtual void close_transmission_channel() = 0;
        };

        virtual center &center_view() = 0;

        virtual worker &worker_view() = 0;

    };
}

#endif //GEMPBA_SCHEDULER_HPP
