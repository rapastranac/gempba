#include <impl/telemetry/topology_builder.hpp>

#include <algorithm>
#include <cstring>
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

            // Only each host's sentinel probes and contributes its layout, so a
            // wide node doesn't multiply the payload across its ranks. Serialize it
            // for a variable-length gather -- no per-socket CPU-ID cap, so wide
            // sockets (e.g. 96-core EPYC) come through intact.
            std::vector<std::uint8_t> v_self_blob;
            {
                const auto v_self_it = v_node_index_by_hostname.find(std::string(v_self.m_hostname));
                const bool v_is_sentinel = v_self_it != v_node_index_by_hostname.end() && v_snapshot.m_nodes[v_self_it->second].m_sentinel_worker_id == p_worker_id;
                if (v_is_sentinel) {
                    topology_node v_self_node;
                    v_self_node.m_hostname = v_self.m_hostname;
                    fill_local_host_fields(v_self_node);
                    detail::serialize_node_topology(v_self_blob, v_self_node);
                }
            }

            const int v_self_size = static_cast<int>(v_self_blob.size());
            std::vector<int> v_sizes(p_world_size);
            MPI_Allgather(&v_self_size, 1, MPI_INT, v_sizes.data(), 1, MPI_INT, MPI_COMM_WORLD);

            std::vector<int> v_displs(p_world_size);
            int v_total = 0;
            for (std::uint32_t v_i = 0; v_i < p_world_size; ++v_i) {
                v_displs[v_i] = v_total;
                v_total += v_sizes[v_i];
            }

            std::vector<std::uint8_t> v_gathered(static_cast<std::size_t>(v_total));
            MPI_Allgatherv(v_self_blob.data(), v_self_size, MPI_BYTE, v_gathered.data(), v_sizes.data(), v_displs.data(), MPI_BYTE, MPI_COMM_WORLD);

            for (std::uint32_t v_i = 0; v_i < p_world_size; ++v_i) {
                if (v_sizes[v_i] <= 0) {
                    continue;
                }
                topology_node v_fragment;
                if (!detail::deserialize_node_topology(v_fragment, v_gathered.data() + v_displs[v_i], static_cast<std::size_t>(v_sizes[v_i]))) {
                    continue;
                }
                const auto v_it = v_node_index_by_hostname.find(v_fragment.m_hostname);
                if (v_it == v_node_index_by_hostname.end()) {
                    continue;
                }
                topology_node& v_node = v_snapshot.m_nodes[v_it->second];
                v_node.m_total_physical_cores = v_fragment.m_total_physical_cores;
                v_node.m_total_logical_cores = v_fragment.m_total_logical_cores;
                v_node.m_mem_total_bytes = v_fragment.m_mem_total_bytes;
                v_node.m_sockets = std::move(v_fragment.m_sockets);
            }

            return v_snapshot;
        }
#endif // GEMPBA_MULTIPROCESSING

    } // namespace

    namespace detail {

        namespace {

            void put_u8(std::vector<std::uint8_t>& p_buf, std::uint8_t p_v) { p_buf.push_back(p_v); }

            void put_u16(std::vector<std::uint8_t>& p_buf, std::uint16_t p_v) {
                p_buf.push_back(static_cast<std::uint8_t>(p_v & 0xFFu));
                p_buf.push_back(static_cast<std::uint8_t>((p_v >> 8) & 0xFFu));
            }

            void put_u32(std::vector<std::uint8_t>& p_buf, std::uint32_t p_v) {
                for (int v_i = 0; v_i < 4; ++v_i) {
                    p_buf.push_back(static_cast<std::uint8_t>((p_v >> (8 * v_i)) & 0xFFu));
                }
            }

            void put_u64(std::vector<std::uint8_t>& p_buf, std::uint64_t p_v) {
                for (int v_i = 0; v_i < 8; ++v_i) {
                    p_buf.push_back(static_cast<std::uint8_t>((p_v >> (8 * v_i)) & 0xFFu));
                }
            }

            void put_str(std::vector<std::uint8_t>& p_buf, const std::string& p_str) {
                const std::uint16_t v_len = static_cast<std::uint16_t>(std::min<std::size_t>(p_str.size(), 0xFFFFu));
                put_u16(p_buf, v_len);
                p_buf.insert(p_buf.end(), p_str.begin(), p_str.begin() + v_len);
            }

            /**
             * @brief Bounds-checked little-endian cursor; m_ok latches false on overrun.
             */
            struct byte_reader {
                const std::uint8_t* m_data = nullptr;
                std::size_t m_size = 0;
                std::size_t m_pos = 0;
                bool m_ok = true;

                bool take(std::size_t p_n) {
                    if (m_pos + p_n > m_size) {
                        m_ok = false;
                        return false;
                    }
                    return true;
                }

                std::uint8_t u8() { return take(1) ? m_data[m_pos++] : 0; }

                std::uint16_t u16() {
                    if (!take(2)) {
                        return 0;
                    }
                    const auto v_value = static_cast<std::uint16_t>(m_data[m_pos] | (m_data[m_pos + 1] << 8));
                    m_pos += 2;
                    return v_value;
                }

                std::uint32_t u32() {
                    if (!take(4)) {
                        return 0;
                    }
                    std::uint32_t v_value = 0;
                    for (int v_i = 0; v_i < 4; ++v_i) {
                        v_value |= static_cast<std::uint32_t>(m_data[m_pos + v_i]) << (8 * v_i);
                    }
                    m_pos += 4;
                    return v_value;
                }

                std::uint64_t u64() {
                    if (!take(8)) {
                        return 0;
                    }
                    std::uint64_t v_value = 0;
                    for (int v_i = 0; v_i < 8; ++v_i) {
                        v_value |= static_cast<std::uint64_t>(m_data[m_pos + v_i]) << (8 * v_i);
                    }
                    m_pos += 8;
                    return v_value;
                }

                std::string str() {
                    const std::uint16_t v_len = u16();
                    if (!take(v_len)) {
                        return {};
                    }
                    std::string v_value(reinterpret_cast<const char*>(m_data + m_pos), v_len);
                    m_pos += v_len;
                    return v_value;
                }
            };

        } // namespace

        void serialize_node_topology(std::vector<std::uint8_t>& p_out, const topology_node& p_node) {
            put_str(p_out, p_node.m_hostname);
            put_u16(p_out, p_node.m_total_physical_cores);
            put_u16(p_out, p_node.m_total_logical_cores);
            put_u64(p_out, p_node.m_mem_total_bytes);
            put_u16(p_out, static_cast<std::uint16_t>(p_node.m_sockets.size()));
            for (const topology_socket& v_socket: p_node.m_sockets) {
                put_u8(p_out, v_socket.m_socket_id);
                put_u16(p_out, v_socket.m_physical_cores);
                put_u16(p_out, v_socket.m_logical_cores);
                put_str(p_out, v_socket.m_name);
                put_u32(p_out, static_cast<std::uint32_t>(v_socket.m_cpu_ids.size()));
                for (const std::uint32_t v_cpu_id: v_socket.m_cpu_ids) {
                    put_u32(p_out, v_cpu_id);
                }
            }
        }

        bool deserialize_node_topology(topology_node& p_node, const std::uint8_t* p_data, std::size_t p_size) {
            byte_reader v_reader{p_data, p_size};
            p_node.m_hostname = v_reader.str();
            p_node.m_total_physical_cores = v_reader.u16();
            p_node.m_total_logical_cores = v_reader.u16();
            p_node.m_mem_total_bytes = v_reader.u64();
            const std::uint16_t v_socket_count = v_reader.u16();

            p_node.m_sockets.clear();
            for (std::uint16_t v_i = 0; v_i < v_socket_count && v_reader.m_ok; ++v_i) {
                topology_socket v_socket;
                v_socket.m_socket_id = v_reader.u8();
                v_socket.m_physical_cores = v_reader.u16();
                v_socket.m_logical_cores = v_reader.u16();
                v_socket.m_name = v_reader.str();
                const std::uint32_t v_cpu_count = v_reader.u32();
                for (std::uint32_t v_j = 0; v_j < v_cpu_count && v_reader.m_ok; ++v_j) {
                    v_socket.m_cpu_ids.push_back(v_reader.u32());
                }
                p_node.m_sockets.push_back(std::move(v_socket));
            }
            return v_reader.m_ok;
        }

    } // namespace detail

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
