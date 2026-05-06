#ifndef GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP

#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/topology.hpp>

#include <cstdint>

namespace gempba::telemetry {

    topology_snapshot build_topology_snapshot(runtime_mode p_mode, std::uint32_t p_worker_id, std::uint32_t p_world_size);

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_BUILDER_HPP
