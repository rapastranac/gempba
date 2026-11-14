#ifndef GEMPBA_TELEMETRY_HWLOC_PROBE_HPP
#define GEMPBA_TELEMETRY_HWLOC_PROBE_HPP

#include <gempba/config.h>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>

#if GEMPBA_HWLOC
    #include <hwloc.h>
#endif

namespace gempba::telemetry {

    // Returns false when GEMPBA_HWLOC is OFF or hwloc fails to load the host
    // topology; callers must provide a fallback path.
    bool fill_static_topology_via_hwloc(topology_node& p_node) noexcept;

    // The package (socket) index owning the logical CPU with OS index p_cpu, or
    // -1 when GEMPBA_HWLOC is OFF / hwloc fails / the CPU is unknown. Matches the
    // socket_id numbering produced by fill_static_topology_via_hwloc.
    int socket_of_cpu_via_hwloc(unsigned p_cpu) noexcept;

    /**
     * @brief The host's hwloc topology, loaded by the first @ref fill_runtime call and kept until destruction.
     *
     * A failed load is not retried.
     */
    class host_topology {
    public:
        host_topology() noexcept = default;
        ~host_topology();

        host_topology(const host_topology&) = delete;
        host_topology& operator=(const host_topology&) = delete;

        /**
         * @brief Fills the sockets of @p p_out from the host topology.
         * @return false when GEMPBA_HWLOC is OFF or the topology failed to load; the caller then provides a fallback.
         */
        bool fill_runtime(node_frame& p_out) noexcept;

    private:
#if GEMPBA_HWLOC
        hwloc_topology_t m_topology = nullptr;
        bool m_load_attempted = false;
#endif
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_HWLOC_PROBE_HPP
