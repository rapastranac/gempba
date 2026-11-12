#ifndef GEMPBA_STOP_STATE_HPP
#define GEMPBA_STOP_STATE_HPP

#include <gempba/core/stop.hpp>
#include <gempba/export.h>
#include <string>

namespace gempba::detail {

    /**
     * @brief Request a stop by @p p_trigger, with the signal's number when it is @ref stop_trigger::SIGNAL.
     * @note Async-signal-safe: one lock-free atomic exchange. The first request wins.
     */
    GEMPBA_API void request_stop(stop_trigger p_trigger, int p_signal = 0) noexcept;

    /**
     * @return @ref end_reason::INTERRUPTED with the first request's trigger and signal, or the default outcome when no stop was
     * requested
     */
    [[nodiscard]] GEMPBA_API run_outcome requested_stop() noexcept;

    /// @brief Forget the stop requested, for the next run in this process.
    GEMPBA_API void clear_stop() noexcept;

    /// @brief Take @c SIGINT and @c SIGTERM where nothing handles them yet, unless @ref stop_on_signals turned that off.
    GEMPBA_API void take_stop_signals();

    /// @brief Give back the signals @ref take_stop_signals took.
    GEMPBA_API void release_stop_signals();

    /// @return @p p_signal's name, such as @c "SIGTERM", or its number for a signal gempba doesn't take
    [[nodiscard]] GEMPBA_API std::string signal_name(int p_signal);

} // namespace gempba::detail

#endif // GEMPBA_STOP_STATE_HPP
