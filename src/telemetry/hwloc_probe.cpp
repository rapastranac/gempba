#include <impl/telemetry/hwloc_probe.hpp>

#if GEMPBA_HWLOC

    #include <algorithm>
    #include <hwloc.h>
    #include <impl/telemetry/hwloc_probe_detail.hpp>
    #include <utility>

namespace gempba::telemetry {

    namespace {

        struct topology_handle {
            hwloc_topology_t m_topology = nullptr;
            bool m_loaded = false;

            topology_handle() noexcept {
                if (hwloc_topology_init(&m_topology) != 0) {
                    m_topology = nullptr;
                    return;
                }
                if (hwloc_topology_load(m_topology) != 0) {
                    hwloc_topology_destroy(m_topology);
                    m_topology = nullptr;
                    return;
                }
                m_loaded = true;
            }

            ~topology_handle() {
                if (m_topology) {
                    hwloc_topology_destroy(m_topology);
                }
            }

            topology_handle(const topology_handle&) = delete;
            topology_handle& operator=(const topology_handle&) = delete;
        };

        std::string read_socket_brand(hwloc_obj_t p_socket) {
            if (const char* v_model = hwloc_obj_get_info_by_name(p_socket, "CPUModel")) {
                return v_model;
            }
            if (p_socket->subtype) {
                return p_socket->subtype;
            }
            return {};
        }

    } // namespace

    namespace detail {

        bool fill_static_topology_from_topology(topology_node& p_node, hwloc_topology_t p_topology) noexcept {
            const int v_socket_count = hwloc_get_nbobjs_by_type(p_topology, HWLOC_OBJ_PACKAGE);
            if (v_socket_count <= 0) {
                return false;
            }

            const int v_pu_count = hwloc_get_nbobjs_by_type(p_topology, HWLOC_OBJ_PU);
            const int v_core_count = hwloc_get_nbobjs_by_type(p_topology, HWLOC_OBJ_CORE);
            if (v_pu_count > 0) {
                p_node.m_total_logical_cores = static_cast<std::uint16_t>(v_pu_count);
            }
            if (v_core_count > 0) {
                p_node.m_total_physical_cores = static_cast<std::uint16_t>(v_core_count);
            }

            p_node.m_sockets.clear();
            p_node.m_sockets.reserve(static_cast<std::size_t>(v_socket_count));

            for (int v_i = 0; v_i < v_socket_count; ++v_i) {
                hwloc_obj_t v_socket = hwloc_get_obj_by_type(p_topology, HWLOC_OBJ_PACKAGE, v_i);
                if (!v_socket || !v_socket->cpuset) {
                    continue;
                }

                topology_socket v_topo_socket;
                v_topo_socket.m_socket_id = static_cast<std::uint8_t>(v_i);
                v_topo_socket.m_name = read_socket_brand(v_socket);

                const int v_phys = hwloc_get_nbobjs_inside_cpuset_by_type(p_topology, v_socket->cpuset, HWLOC_OBJ_CORE);
                const int v_log = hwloc_get_nbobjs_inside_cpuset_by_type(p_topology, v_socket->cpuset, HWLOC_OBJ_PU);
                v_topo_socket.m_physical_cores = (v_phys > 0) ? static_cast<std::uint16_t>(v_phys) : 0;
                v_topo_socket.m_logical_cores = (v_log > 0) ? static_cast<std::uint16_t>(v_log) : 0;

                v_topo_socket.m_cpu_ids.reserve(static_cast<std::size_t>(v_log > 0 ? v_log : 0));
                hwloc_obj_t v_pu = nullptr;
                while ((v_pu = hwloc_get_next_obj_inside_cpuset_by_type(p_topology, v_socket->cpuset, HWLOC_OBJ_PU, v_pu)) != nullptr) {
                    v_topo_socket.m_cpu_ids.push_back(static_cast<std::uint32_t>(v_pu->os_index));
                }

                p_node.m_sockets.push_back(std::move(v_topo_socket));
            }

            return !p_node.m_sockets.empty();
        }

        bool fill_runtime_from_topology(node_frame& p_out, hwloc_topology_t p_topology) noexcept {
            const int v_socket_count = hwloc_get_nbobjs_by_type(p_topology, HWLOC_OBJ_PACKAGE);
            if (v_socket_count <= 0) {
                return false;
            }

            const int v_clamped = std::min<int>(v_socket_count, static_cast<int>(MAX_SOCKETS));
            p_out.m_socket_count = static_cast<std::uint8_t>(v_clamped);

            for (int v_i = 0; v_i < v_clamped; ++v_i) {
                hwloc_obj_t v_socket = hwloc_get_obj_by_type(p_topology, HWLOC_OBJ_PACKAGE, v_i);
                socket_stats& v_stats = p_out.m_sockets[v_i];
                v_stats.m_socket_id = static_cast<std::uint8_t>(v_i);
                v_stats.m_cpu_pct = 0.0f;
                v_stats.m_mem_total_bytes = 0;
                v_stats.m_mem_used_bytes = 0;

                if (!v_socket || !v_socket->cpuset) {
                    continue;
                }

                // Sub-NUMA clustering (e.g. EPYC NPS=2/4) presents multiple NUMA
                // nodes per package; collapse their local_memory back to socket
                // level so per-socket totals match what users expect.
                hwloc_obj_t v_numa = nullptr;
                while ((v_numa = hwloc_get_next_obj_by_type(p_topology, HWLOC_OBJ_NUMANODE, v_numa)) != nullptr) {
                    if (v_numa->cpuset && hwloc_bitmap_isincluded(v_numa->cpuset, v_socket->cpuset)) {
                        v_stats.m_mem_total_bytes += static_cast<std::uint64_t>(v_numa->attr ? v_numa->attr->numanode.local_memory : 0);
                    }
                }
            }

            return true;
        }

    } // namespace detail

    bool fill_static_topology_via_hwloc(topology_node& p_node) noexcept {
        topology_handle v_handle;
        if (!v_handle.m_loaded) {
            return false; // LCOV_EXCL_LINE
        }
        return detail::fill_static_topology_from_topology(p_node, v_handle.m_topology);
    }

    bool fill_runtime_via_hwloc(node_frame& p_out) noexcept {
        topology_handle v_handle;
        if (!v_handle.m_loaded) {
            return false; // LCOV_EXCL_LINE
        }
        return detail::fill_runtime_from_topology(p_out, v_handle.m_topology);
    }

} // namespace gempba::telemetry

#else // !GEMPBA_HWLOC

namespace gempba::telemetry {

    bool fill_static_topology_via_hwloc(topology_node& /*p_node*/) noexcept { return false; }
    bool fill_runtime_via_hwloc(node_frame& /*p_out*/) noexcept { return false; }

} // namespace gempba::telemetry

#endif // GEMPBA_HWLOC
