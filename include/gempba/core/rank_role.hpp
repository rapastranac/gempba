#ifndef GEMPBA_RANK_ROLE_HPP
#define GEMPBA_RANK_ROLE_HPP

#include <cstdint>

namespace gempba {

    /**
     * @brief What a rank does in its scheduler, as @ref scheduler::role_of reports it.
     *
     * A scheduler role only. Telemetry's aggregator, the rank that collects every rank's frames and serves the dashboard, is always rank 0, whatever its role here.
     */
    enum class rank_role : std::uint8_t {
        /// Runs tasks.
        WORKER,
        /// Coordinates the run and runs no tasks.
        CENTER,
    };

} // namespace gempba

#endif // GEMPBA_RANK_ROLE_HPP
