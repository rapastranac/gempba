#ifndef GEMPBA_TELEMETRY_PROCESS_PROBE_HPP
#define GEMPBA_TELEMETRY_PROCESS_PROBE_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>

namespace gempba::telemetry {

    /**
     * @brief Process-level signal sampler that fills part of a @ref worker_frame.
     *
     * Single-threaded by construction. @c m_process_cpu_pct is a delta between
     * consecutive @ref sample calls, so the first sample on a fresh instance
     * always reports 0. Fills @c m_process_cpu_pct and @c m_process_rss_bytes
     * only; @c m_process_threads is filled by the hub from the load balancer.
     */
    class process_probe {
    public:
        process_probe() noexcept;
        ~process_probe() = default;

        process_probe(const process_probe&) = delete;
        process_probe& operator=(const process_probe&) = delete;

        void sample(worker_frame& p_out) noexcept;

    private:
        std::uint64_t m_last_cpu_microseconds = 0;
        std::uint64_t m_last_wall_microseconds = 0;
        bool m_has_baseline = false;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_PROCESS_PROBE_HPP
