#ifndef GEMPBA_TELEMETRY_PROCESS_PROBE_HPP
#define GEMPBA_TELEMETRY_PROCESS_PROBE_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>

namespace gempba::telemetry {

    // Single-threaded. process_cpu_pct is a delta between consecutive samples,
    // so the first sample on a fresh instance always reports 0.
    class process_probe {
    public:
        process_probe() noexcept;
        ~process_probe() = default;

        process_probe(const process_probe&) = delete;
        process_probe& operator=(const process_probe&) = delete;

        // Fills process_cpu_pct and process_rss_bytes; leaves process_threads
        // untouched (the hub fills it from the load_balancer).
        void sample(worker_frame& p_out) noexcept;

    private:
        std::uint64_t m_last_cpu_microseconds = 0;
        std::uint64_t m_last_wall_microseconds = 0;
        bool m_has_baseline = false;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_PROCESS_PROBE_HPP
