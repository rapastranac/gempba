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

    };
}

#endif //GEMPBA_SCHEDULER_HPP
