#ifndef GEMPBA_TELEMETRY_HWLOC_PROBE_HPP
#define GEMPBA_TELEMETRY_HWLOC_PROBE_HPP

#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>

namespace gempba::telemetry {

    // Both functions return false when GEMPBA_HWLOC is OFF or hwloc fails to
    // load the host topology; callers must provide a fallback path.
    bool fill_static_topology_via_hwloc(topology_node& p_node) noexcept;
    bool fill_runtime_via_hwloc(node_frame& p_out) noexcept;

    // The package (socket) index owning the logical CPU with OS index p_cpu, or
    // -1 when GEMPBA_HWLOC is OFF / hwloc fails / the CPU is unknown. Matches the
    // socket_id numbering produced by fill_static_topology_via_hwloc.
    int socket_of_cpu_via_hwloc(unsigned p_cpu) noexcept;

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_HWLOC_PROBE_HPP
