#ifndef GEMPBA_STOP_HPP
#define GEMPBA_STOP_HPP

#include <cstdint>
#include <gempba/export.h>

namespace gempba {

    /** @brief How a run ended. */
    enum class end_reason : std::uint8_t {
        /// The search space was exhausted, so the result is proven.
        COMPLETED,
        /// A stop was requested, so the result is the best one found before it.
        INTERRUPTED,
    };

    /** @brief What requested the stop that interrupted a run. */
    enum class stop_trigger : std::uint8_t {
        /// No stop was requested.
        NONE,
        /// The program called @ref request_stop.
        CODE,
        /// @c SIGINT or @c SIGTERM arrived; see @ref stop_on_signals.
        SIGNAL,
        /// A stop control line arrived on the telemetry socket.
        CONTROL,
    };

    /** @brief How a run ended, as @ref scheduler::run returns it. */
    struct run_outcome {
        end_reason m_reason = end_reason::COMPLETED;
        stop_trigger m_trigger = stop_trigger::NONE;
        /// The signal's number when @ref m_trigger is @ref stop_trigger::SIGNAL; 0 otherwise.
        int m_signal = 0;

        bool operator==(const run_outcome&) const = default;
    };

    /**
     * @brief Ask the run to stop. Returns at once and is safe from any thread.
     *
     * Nodes that haven't started are pruned from then on, nodes already running finish their own work, and the run returns its best
     * result so far. Under MPI every rank stops. The first stop requested wins, by any trigger, until @ref shutdown.
     */
    GEMPBA_API void request_stop() noexcept;

    /**
     * @return whether a stop was requested since the last @ref shutdown, by any trigger
     * @note Long work inside one node can poll it to return early.
     */
    [[nodiscard]] GEMPBA_API bool stop_requested() noexcept;

    /**
     * @brief Whether @c SIGINT and @c SIGTERM stop the run gracefully instead of killing the process; on by default.
     *
     * While on, @ref create_node_manager takes each of the two signals that nothing else handles yet, and @ref shutdown gives it
     * back. A second @c SIGINT kills the process. Turning it off gives the signals back at once, so they kill the process again.
     */
    GEMPBA_API void stop_on_signals(bool p_enabled);

    namespace detail {
        /**
         * @brief Log how the run ended: @c info for a completed run, @c warning for an interrupted one, naming what stopped it.
         * @ref scheduler::run calls it on rank 0, and @ref shutdown for a run without a scheduler.
         */
        GEMPBA_API void log_run_end(const run_outcome& p_outcome);
    } // namespace detail

} // namespace gempba

#endif // GEMPBA_STOP_HPP
