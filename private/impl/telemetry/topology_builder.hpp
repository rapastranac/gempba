#ifndef GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP

#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/topology.hpp>

#include <cstdint>

namespace gempba::telemetry {

    /**
     * @brief Build the static topology snapshot for the current runtime.
     *
     * Called once by @ref telemetry_hub::on_runtime_ready. The MT-only path
     * synthesizes a single-node snapshot; the multi-process path queries each
     * rank and merges their identities into one snapshot.
     */
    topology_snapshot build_topology_snapshot(runtime_mode p_mode, std::uint32_t p_worker_id, std::uint32_t p_world_size);

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
