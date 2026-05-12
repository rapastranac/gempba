#include <impl/telemetry/topology_builder.hpp>

#include <algorithm>
#include <cstring>
#include <gempba/config.h>
#include <impl/telemetry/hwloc_probe.hpp>
#include <map>
#include <thread>
#include <vector>

#if GEMPBA_MULTIPROCESSING
    #include <mpi.h>
#endif

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <sys/sysctl.h>
    #include <unistd.h>
#else
    #include <fstream>
    #include <sched.h>
    #include <string>
    #include <unistd.h>
#endif

namespace gempba::telemetry {

    namespace {

        void fill_hostname(char* p_buf, std::size_t p_buf_size) noexcept {
            if (p_buf_size == 0)
                return;
            p_buf[0] = '\0';
#if defined(_WIN32)
            auto v_size = static_cast<DWORD>(p_buf_size);
            if (!GetComputerNameA(p_buf, &v_size)) {
                std::strncpy(p_buf, "unknown", p_buf_size - 1);
                p_buf[p_buf_size - 1] = '\0';
            }
#else
            if (gethostname(p_buf, p_buf_size) != 0) {
                std::strncpy(p_buf, "unknown", p_buf_size - 1);
            }
            p_buf[p_buf_size - 1] = '\0';
#endif
        }

        std::uint32_t current_pid() noexcept {
#if defined(_WIN32)
            return static_cast<std::uint32_t>(GetCurrentProcessId());
#else
            return static_cast<std::uint32_t>(getpid());
#endif
        }

        std::string trim_copy(const std::string& p_in) {
            const auto v_first = p_in.find_first_not_of(" \t");
            if (v_first == std::string::npos)
                return {};
            const auto v_last = p_in.find_last_not_of(" \t\r\n");
            return p_in.substr(v_first, v_last - v_first + 1);
        }

        std::string read_cpu_brand() noexcept {
#if defined(_WIN32)
            // Registry path works on x86 and ARM Windows; avoids the x86-only
            // __cpuid intrinsic.
            HKEY v_key{};
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)", 0, KEY_READ, &v_key) != ERROR_SUCCESS) {
                return {};
            }
            char v_buf[256]{};
            DWORD v_sz = sizeof(v_buf);
            const LSTATUS v_st = RegQueryValueExA(v_key, "ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(v_buf), &v_sz);
            RegCloseKey(v_key);
            if (v_st != ERROR_SUCCESS)
                return {};
            return trim_copy(v_buf);
#elif defined(__APPLE__)
            char v_buf[128]{};
            std::size_t v_sz = sizeof(v_buf);
            if (sysctlbyname("machdep.cpu.brand_string", v_buf, &v_sz, nullptr, 0) == 0) {
                return trim_copy(v_buf);
            }
            // Apple Silicon falls back to hw.model on some macOS versions.
            v_sz = sizeof(v_buf);
            if (sysctlbyname("hw.model", v_buf, &v_sz, nullptr, 0) == 0) {
                return trim_copy(v_buf);
            }
            return {};
#else
            std::ifstream v_in("/proc/cpuinfo");
            if (!v_in)
                return {};
            std::string v_line;
            while (std::getline(v_in, v_line)) {
                if (v_line.rfind("model name", 0) == 0) {
                    const auto v_colon = v_line.find(':');
                    if (v_colon != std::string::npos) {
                        return trim_copy(v_line.substr(v_colon + 1));
                    }
                }
            }
            return {};
#endif
        }

        // Bitmap of logical CPUs the current process is allowed to run on.
        // Capped at 64 to fit worker_identity's POD layout.
        std::uint64_t query_allowed_cpu_mask() noexcept {
#if defined(_WIN32)
            DWORD_PTR v_proc_mask = 0;
            DWORD_PTR v_sys_mask = 0;
            if (GetProcessAffinityMask(GetCurrentProcess(), &v_proc_mask, &v_sys_mask)) {
                return static_cast<std::uint64_t>(v_proc_mask);
            }
            return 0;
#elif defined(__APPLE__)
            // macOS does not expose process CPU affinity (Apple discourages
            // pinning). Report "any CPU" within the visible core count.
            const unsigned v_cores = std::thread::hardware_concurrency();
            if (v_cores == 0 || v_cores >= 64)
                return ~static_cast<std::uint64_t>(0);
            return (static_cast<std::uint64_t>(1) << v_cores) - 1;
#else
            cpu_set_t v_set;
            CPU_ZERO(&v_set);
            if (sched_getaffinity(0, sizeof(v_set), &v_set) != 0)
                return 0;
            std::uint64_t v_mask = 0;
            const unsigned v_max = std::min(64u, static_cast<unsigned>(CPU_SETSIZE));
            for (unsigned v_i = 0; v_i < v_max; ++v_i) {
                if (CPU_ISSET(static_cast<int>(v_i), &v_set)) {
                    v_mask |= (1ULL << v_i);
                }
            }
            return v_mask;
#endif
        }

        std::uint64_t query_total_memory_bytes() noexcept {
#if defined(_WIN32)
            MEMORYSTATUSEX v_status{};
            v_status.dwLength = sizeof(v_status);
            if (GlobalMemoryStatusEx(&v_status))
                return v_status.ullTotalPhys;
            return 0;
#elif defined(__APPLE__)
            std::uint64_t v_total = 0;
            std::size_t v_sz = sizeof(v_total);
            if (sysctlbyname("hw.memsize", &v_total, &v_sz, nullptr, 0) == 0) {
                return v_total;
            }
            return 0;
#else
            std::ifstream v_in("/proc/meminfo");
            if (!v_in)
                return 0;
            std::string v_line;
            while (std::getline(v_in, v_line)) {
                if (v_line.rfind("MemTotal:", 0) == 0) {
                    const auto v_pos = v_line.find_first_of("0123456789");
                    if (v_pos != std::string::npos) {
                        return static_cast<std::uint64_t>(std::stoull(v_line.substr(v_pos))) * 1024ULL;
                    }
                }
            }
            return 0;
#endif
        }

        worker_identity build_self_identity(std::uint32_t p_worker_id) noexcept {
            worker_identity v_self{};
            v_self.m_version = TELEMETRY_SCHEMA_VERSION;
            v_self.m_worker_id = p_worker_id;
            fill_hostname(v_self.m_hostname, sizeof(v_self.m_hostname));
            v_self.m_pid = current_pid();
            v_self.m_allowed_cpu_mask = query_allowed_cpu_mask();
            v_self.m_primary_socket = 0;
            return v_self;
        }

        // Builds the local-host topology_node fields the current rank can fill
        // from its own probes (logical cores, total memory, CPU brand, cpu_ids).
        // Used both for the only node in MT-only mode and for the rank's home
        // node in MPI mode.
        void fill_local_host_fields(topology_node& p_node) noexcept {
            p_node.m_mem_total_bytes = query_total_memory_bytes();

            if (fill_static_topology_via_hwloc(p_node)) {
                return;
            }

            const unsigned v_logical = std::thread::hardware_concurrency();
            const auto v_logical_clamped = static_cast<std::uint16_t>(v_logical == 0 ? 1u : v_logical);
            p_node.m_total_logical_cores = v_logical_clamped;
            p_node.m_total_physical_cores = 0;

            topology_socket v_socket;
            v_socket.m_socket_id = 0;
            v_socket.m_name = read_cpu_brand();
            v_socket.m_physical_cores = 0;
            v_socket.m_logical_cores = v_logical_clamped;
            v_socket.m_cpu_ids.reserve(v_logical_clamped);
            for (std::uint32_t v_i = 0; v_i < v_logical_clamped; ++v_i) {
                v_socket.m_cpu_ids.push_back(v_i);
            }
            p_node.m_sockets.push_back(std::move(v_socket));
        }

        topology_snapshot build_mt_only_snapshot(std::uint32_t p_worker_id) {
            topology_snapshot v_snapshot;
            const worker_identity v_self = build_self_identity(p_worker_id);
            v_snapshot.m_identities.push_back(v_self);

            topology_node v_node;
            v_node.m_hostname = v_self.m_hostname;
            v_node.m_worker_ids.push_back(p_worker_id);
            v_node.m_sentinel_worker_id = p_worker_id;
            fill_local_host_fields(v_node);
            v_snapshot.m_nodes.push_back(std::move(v_node));
            return v_snapshot;
        }

#if GEMPBA_MULTIPROCESSING
        topology_snapshot build_mpi_snapshot(std::uint32_t p_worker_id, std::uint32_t p_world_size) {
            topology_snapshot v_snapshot;
            const worker_identity v_self = build_self_identity(p_worker_id);

            int v_initialized = 0;
            MPI_Initialized(&v_initialized);
            if (!v_initialized || p_world_size <= 1) {
                v_snapshot.m_identities.push_back(v_self);
                topology_node v_node;
                v_node.m_hostname = v_self.m_hostname;
                v_node.m_worker_ids.push_back(p_worker_id);
                v_node.m_sentinel_worker_id = p_worker_id;
                fill_local_host_fields(v_node);
                v_snapshot.m_nodes.push_back(std::move(v_node));
                return v_snapshot;
            }

            std::vector<worker_identity> v_all(p_world_size);
            MPI_Allgather(&v_self, sizeof(worker_identity), MPI_BYTE, v_all.data(), sizeof(worker_identity), MPI_BYTE, MPI_COMM_WORLD);
            v_snapshot.m_identities = v_all;

            std::map<std::string, std::size_t> v_node_index_by_hostname;
            for (const auto& v_id: v_all) {
                const std::string v_hostname(v_id.m_hostname);
                auto v_it = v_node_index_by_hostname.find(v_hostname);
                if (v_it == v_node_index_by_hostname.end()) {
                    topology_node v_node;
                    v_node.m_hostname = v_hostname;
                    v_node.m_worker_ids.push_back(v_id.m_worker_id);
                    v_node.m_sentinel_worker_id = v_id.m_worker_id;
                    if (v_hostname == std::string(v_self.m_hostname)) {
                        fill_local_host_fields(v_node);
                    }
                    v_node_index_by_hostname.emplace(v_hostname, v_snapshot.m_nodes.size());
                    v_snapshot.m_nodes.push_back(std::move(v_node));
                } else {
                    topology_node& v_existing = v_snapshot.m_nodes[v_it->second];
                    v_existing.m_worker_ids.push_back(v_id.m_worker_id);
                    if (v_id.m_worker_id < v_existing.m_sentinel_worker_id) {
                        v_existing.m_sentinel_worker_id = v_id.m_worker_id;
                    }
                }
            }

            return v_snapshot;
        }
#endif // GEMPBA_MULTIPROCESSING

    } // namespace

    topology_snapshot build_topology_snapshot(const runtime_mode p_mode, const std::uint32_t p_worker_id, const std::uint32_t p_world_size) {
        switch (p_mode) {
            case runtime_mode::MT_ONLY:
                return build_mt_only_snapshot(p_worker_id);
#if GEMPBA_MULTIPROCESSING
            case runtime_mode::MP_MPI:
                return build_mpi_snapshot(p_worker_id, p_world_size);
#else
            case runtime_mode::MP_MPI:
                return build_mt_only_snapshot(p_worker_id);
#endif
        }
        return build_mt_only_snapshot(p_worker_id);
    }

} // namespace gempba::telemetry
