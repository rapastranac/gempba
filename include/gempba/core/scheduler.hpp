#ifndef GEMPBA_SCHEDULER_HPP
#define GEMPBA_SCHEDULER_HPP

#include <cstddef>
#include <functional>
#include <gempba/core/rank_role.hpp>
#include <gempba/core/scheduler_traits.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/core/stop.hpp>
#include <gempba/utils/gempba_utils.hpp>
#include <gempba/utils/result.hpp>
#include <gempba/utils/score.hpp>
#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/tree.hpp>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gempba {

    class result_holder_parent;

    class node_manager;

    /**
     * @brief The base of every scheduler; pass a custom one to @c gempba::mpi::create_scheduler.
     *
     * docs/telemetry/custom-schedulers.md says what a custom scheduler must do for its runs to reach the dashboard: be created on every rank,
     * return the @c MPI_COMM_WORLD rank from @c rank_me(), call @c telemetry_hub::tick_if_due() from its own loops, and report what happens
     * with the hub's @c record_* calls. Those calls are the only link to telemetry; this interface carries no telemetry bookkeeping.
     */
    class scheduler : public scheduler_traits {
    protected:
        scheduler() = default;

    public:
        ~scheduler() override = default;

        [[nodiscard]] virtual double elapsed_time() const = 0;

        [[nodiscard]] virtual std::size_t get_pending_request_count() const = 0;

        /**
         * @return the name telemetry reports for this scheduler; "custom" for schedulers that do not name themselves
         */
        [[nodiscard]] virtual std::string_view get_name() const { return "custom"; }

        /**
         * @param p_rank a rank in [0, world_size())
         * @return the role @p p_rank plays in this scheduler; @ref rank_role::WORKER for every rank of schedulers that do not say otherwise
         * @note Each rank reports its own role, @c role_of(rank_me()), to telemetry once, at startup, so the answer must stay fixed for the
         * whole run.
         */
        [[nodiscard]] virtual rank_role role_of([[maybe_unused]] int p_rank) const { return rank_role::WORKER; }

        virtual void set_goal(goal p_goal, score_type p_type) = 0;

        virtual void set_custom_initial_topology(tree&& p_tree) = 0;

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

        /**
         * @return whether this rank is the root, rank 0: the rank that seeds the run and reads its results
         */
        [[nodiscard]] bool is_root() const { return rank_me() == 0; }

        /**
         * @brief Runs this rank's part of the search; every rank calls it and returns once the whole run has ended.
         *
         * @param p_node_manager this rank's node manager
         * @param p_runnables the runnables by id, the same on every rank
         * @param p_seed the root's first task; absent on every other rank
         * @return how the run ended, the same on every rank
         * @throws std::invalid_argument if the root passes no seed or another rank passes one
         */
        run_outcome run(node_manager& p_node_manager, std::map<int, std::shared_ptr<serial_runnable>> p_runnables, std::optional<task_bundle> p_seed = std::nullopt) {
            if (is_root() != p_seed.has_value()) {
                throw std::invalid_argument(is_root() ? "the root must pass a seed to scheduler::run" : "only the root passes a seed to scheduler::run");
            }
            return do_run(p_node_manager, std::move(p_runnables), std::move(p_seed));
        }

        /**
         * @return the best result found across all ranks, once @ref run has returned
         * @throws std::logic_error on any rank but the root
         */
        [[nodiscard]] task_packet get_result() {
            require_root("scheduler::get_result");
            return do_get_result();
        }

        /**
         * @return each rank's best result, indexed by rank and empty where a rank found none, once @ref run has returned
         * @throws std::logic_error on any rank but the root
         */
        [[nodiscard]] std::vector<result> get_all_results() {
            require_root("scheduler::get_all_results");
            return do_get_all_results();
        }

        /**
         * @brief Hands @p p_task to another rank, to run under runnable @p p_function_id.
         * @return the rank the task is sent to
         */
        virtual unsigned int force_push(task_packet&& p_task, int p_function_id) = 0;

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

    protected:
        /**
         * @brief This rank's part of @ref run; @p p_seed is set exactly when this rank is the root.
         * @return how the run ended, the same on every rank: @ref end_reason::INTERRUPTED once a stop was requested on any rank
         */
        virtual run_outcome do_run(node_manager& p_node_manager, std::map<int, std::shared_ptr<serial_runnable>> p_runnables, std::optional<task_bundle> p_seed) = 0;

        /// @brief @ref get_result on the root.
        virtual task_packet do_get_result() = 0;

        /// @brief @ref get_all_results on the root.
        virtual std::vector<result> do_get_all_results() = 0;

    private:
        void require_root(const std::string_view p_call) const {
            if (!is_root()) {
                throw std::logic_error(std::string(p_call) + " is only available on the root, rank 0");
            }
        }
    };
} // namespace gempba

#endif // GEMPBA_SCHEDULER_HPP
