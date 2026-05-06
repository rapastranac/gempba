#ifndef GEMPBA_TELEMETRY_TOPOLOGY_HPP
#define GEMPBA_TELEMETRY_TOPOLOGY_HPP

#include <gempba/telemetry/frames.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gempba::telemetry {

    struct topology_socket {
        std::uint8_t m_socket_id = 0;
        std::string m_name;
        std::uint16_t m_physical_cores = 0; // 0 when GEMPBA_HWLOC is OFF
        std::uint16_t m_logical_cores = 0;
        std::vector<std::uint32_t> m_cpu_ids;
    };

    struct topology_node {
        std::string m_hostname;
        std::vector<std::uint32_t> m_worker_ids;
        std::uint32_t m_sentinel_worker_id = 0;
        std::vector<topology_socket> m_sockets;
        std::uint16_t m_total_physical_cores = 0; // 0 when GEMPBA_HWLOC is OFF
        std::uint16_t m_total_logical_cores = 0;
        std::uint64_t m_mem_total_bytes = 0;
    };

    struct topology_snapshot {
        std::vector<topology_node> m_nodes;
        std::vector<worker_identity> m_identities;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TOPOLOGY_HPP
