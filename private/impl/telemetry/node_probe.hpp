#ifndef GEMPBA_TELEMETRY_NODE_PROBE_HPP
#define GEMPBA_TELEMETRY_NODE_PROBE_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>

namespace gempba::telemetry {

    /**
     * @brief Host-level signal sampler that fills a @ref node_frame.
     *
     * Driven by the sentinel worker only, at the node-frame cadence.
     * Per-socket CPU% and net/disk aggregates are placeholders pending
     * platform-specific sampling.
     */
    class node_probe {
    public:
        node_probe() noexcept;
        ~node_probe() = default;

        node_probe(const node_probe&) = delete;
        node_probe& operator=(const node_probe&) = delete;

        void sample(node_frame& p_out) noexcept;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_NODE_PROBE_HPP
