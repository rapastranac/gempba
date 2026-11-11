#ifndef GEMPBA_TELEMETRY_HWLOC_PROBE_DETAIL_HPP
#define GEMPBA_TELEMETRY_HWLOC_PROBE_DETAIL_HPP

#include <gempba/config.h>

#if GEMPBA_HWLOC

    #include <gempba/telemetry/frames.hpp>
    #include <gempba/telemetry/topology.hpp>
    #include <hwloc.h>

namespace gempba::telemetry::detail {

    // Internal probe primitives operating on a caller-owned, already-loaded
    // hwloc topology. The public fill_*_via_hwloc entry points wrap these with
    // their own handle. Tests construct synthetic topologies directly to
    // exercise every branch without depending on host hardware.
    bool fill_static_topology_from_topology(topology_node& p_node, hwloc_topology_t p_topology) noexcept;
    bool fill_runtime_from_topology(node_frame& p_out, hwloc_topology_t p_topology) noexcept;

} // namespace gempba::telemetry::detail

#endif // GEMPBA_HWLOC

#endif // GEMPBA_TELEMETRY_HWLOC_PROBE_DETAIL_HPP
