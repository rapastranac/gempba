#ifndef GEMPBA_TELEMETRY_RUNTIME_MODE_HPP
#define GEMPBA_TELEMETRY_RUNTIME_MODE_HPP

namespace gempba::telemetry {

    enum class runtime_mode {
        MT_ONLY,
        MP_MPI,
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_RUNTIME_MODE_HPP
