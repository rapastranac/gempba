#ifndef GEMPBA_TELEMETRY_NODE_PROBE_HPP
#define GEMPBA_TELEMETRY_NODE_PROBE_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>

namespace gempba::telemetry {

    // Driven by the sentinel only, at the node_frame cadence.
    class node_probe {
    public:
        node_probe() noexcept;
        ~node_probe() = default;

        node_probe(const node_probe&) = delete;
        node_probe& operator=(const node_probe&) = delete;

        // Per-socket m_cpu_pct and net/disk aggregates stay zero pending
        // platform-specific sampling.
        void sample(node_frame& p_out) noexcept;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_NODE_PROBE_HPP
