#ifndef GEMPBA_TELEMETRY_RUNTIME_MODE_HPP
#define GEMPBA_TELEMETRY_RUNTIME_MODE_HPP

namespace gempba::telemetry {

    /**
     * @brief Runtime mode the telemetry hub was installed under.
     *
     * Set once by @ref telemetry_hub::on_runtime_ready and used to pick the
     * transport and topology layout.
     */
    enum class runtime_mode {
        /// Single-process run; one worker, one host.
        MT_ONLY,
        /// Multi-process run; one worker per rank, one or more hosts.
        MP_MPI,
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_RUNTIME_MODE_HPP
