#include <impl/telemetry/node_probe.hpp>

#include <cstring>
#include <impl/telemetry/cgroup_memory.hpp>
#include <impl/telemetry/hwloc_probe.hpp>
#include <thread>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach/host_info.h>
    #include <mach/mach.h>
    #include <mach/mach_host.h>
    #include <mach/vm_statistics.h>
    #include <sys/sysctl.h>
    #include <unistd.h>
#else
    #include <fstream>
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

#if defined(_WIN32)
        void fill_memory(std::uint64_t& p_total_bytes, std::uint64_t& p_avail_bytes) noexcept {
            MEMORYSTATUSEX v_status{};
            v_status.dwLength = sizeof(v_status);
            if (GlobalMemoryStatusEx(&v_status)) {
                p_total_bytes = v_status.ullTotalPhys;
                p_avail_bytes = v_status.ullAvailPhys;
            } else {
                p_total_bytes = 0;
                p_avail_bytes = 0;
            }
        }
#elif defined(__APPLE__)
        void fill_memory(std::uint64_t& p_total_bytes, std::uint64_t& p_avail_bytes) noexcept {
            p_total_bytes = 0;
            p_avail_bytes = 0;

            std::uint64_t v_total = 0;
            std::size_t v_total_sz = sizeof(v_total);
            if (sysctlbyname("hw.memsize", &v_total, &v_total_sz, nullptr, 0) == 0) {
                p_total_bytes = v_total;
            }

            // "Available" on macOS = free + inactive pages: pages that aren't
            // dirty and can be reclaimed without paging.
            const mach_port_t v_host = mach_host_self();
            vm_size_t v_pagesize = 0;
            if (host_page_size(v_host, &v_pagesize) != KERN_SUCCESS)
                return;

            vm_statistics64_data_t v_stat{};
            mach_msg_type_number_t v_count = HOST_VM_INFO64_COUNT;
            if (host_statistics64(v_host, HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&v_stat), &v_count) == KERN_SUCCESS) {
                p_avail_bytes = (static_cast<std::uint64_t>(v_stat.free_count) + static_cast<std::uint64_t>(v_stat.inactive_count)) * static_cast<std::uint64_t>(v_pagesize);
            }
        }
#else
        // MemAvailable has been present since Linux 3.14; older kernels fall
        // back to MemFree, which under-reports usable memory but never crashes.
        void fill_memory(std::uint64_t& p_total_bytes, std::uint64_t& p_avail_bytes) noexcept {
            p_total_bytes = 0;
            p_avail_bytes = 0;
            std::ifstream v_meminfo("/proc/meminfo");
            if (!v_meminfo)
                return;

            std::uint64_t v_free_fallback = 0;
            std::string v_line;
            while (std::getline(v_meminfo, v_line)) {
                const auto v_consume = [&v_line](const char* p_key) -> std::uint64_t {
                    const std::size_t v_key_len = std::strlen(p_key);
                    if (v_line.compare(0, v_key_len, p_key) != 0)
                        return 0;
                    const auto v_pos = v_line.find_first_of("0123456789");
                    if (v_pos == std::string::npos)
                        return 0;
                    return static_cast<std::uint64_t>(std::stoull(v_line.substr(v_pos))) * 1024ULL;
                };
                if (auto v_v = v_consume("MemTotal:"))
                    p_total_bytes = v_v;
                else if (auto v_v = v_consume("MemAvailable:"))
                    p_avail_bytes = v_v;
                else if (auto v_v = v_consume("MemFree:"))
                    v_free_fallback = v_v;
            }
            if (p_avail_bytes == 0)
                p_avail_bytes = v_free_fallback;
        }
#endif

#if defined(_WIN32) || defined(__APPLE__)
        void fill_cgroup_memory(std::uint64_t /*p_host_total*/, std::uint64_t& p_used, std::uint64_t& p_limit) noexcept {
            p_used = 0;
            p_limit = 0;
        }
#else
        // The job's memory-cgroup usage and limit, or 0 when unconstrained so the
        // consumer falls back to the host total. Scheduler-agnostic; tries cgroup
        // v2 then v1, resolving the limit at the enforcing ancestor via
        // detail::resolve_cgroup_memory.
        void fill_cgroup_memory(std::uint64_t p_host_total, std::uint64_t& p_used, std::uint64_t& p_limit) noexcept {
            p_used = 0;
            p_limit = 0;

            std::ifstream v_cgroup("/proc/self/cgroup");
            if (!v_cgroup)
                return;

            std::string v_line;
            std::string v_v2_path;
            std::string v_v1_memory_path;
            while (std::getline(v_cgroup, v_line)) {
                const auto v_first_colon = v_line.find(':');
                if (v_first_colon == std::string::npos)
                    continue;
                const auto v_second_colon = v_line.find(':', v_first_colon + 1);
                if (v_second_colon == std::string::npos)
                    continue;
                const std::string v_controllers = v_line.substr(v_first_colon + 1, v_second_colon - v_first_colon - 1);
                const std::string v_path = v_line.substr(v_second_colon + 1);
                if (v_controllers.empty()) {
                    v_v2_path = v_path; // cgroup v2 unified hierarchy
                } else if (v_controllers.find("memory") != std::string::npos) {
                    v_v1_memory_path = v_path;
                }
            }

            if (!v_v2_path.empty()) {
                detail::resolve_cgroup_memory("/sys/fs/cgroup" + v_v2_path, "/sys/fs/cgroup", "memory.max", "memory.current", p_host_total, p_used, p_limit);
            }
            if (p_limit == 0 && p_used == 0 && !v_v1_memory_path.empty()) {
                detail::resolve_cgroup_memory("/sys/fs/cgroup/memory" + v_v1_memory_path, "/sys/fs/cgroup/memory", "memory.limit_in_bytes", "memory.usage_in_bytes", p_host_total, p_used,
                                              p_limit);
            }
        }
#endif

    } // namespace

    node_probe::node_probe() noexcept = default;

    void node_probe::sample(node_frame& p_out) noexcept {
        fill_hostname(p_out.m_hostname, sizeof(p_out.m_hostname));

        const unsigned v_cores = std::thread::hardware_concurrency();
        p_out.m_logical_cores = (v_cores == 0) ? 1u : v_cores;

        fill_memory(p_out.m_mem_total_bytes, p_out.m_mem_avail_bytes);
        fill_cgroup_memory(p_out.m_mem_total_bytes, p_out.m_cgroup_mem_used_bytes, p_out.m_cgroup_mem_limit_bytes);

        if (!fill_runtime_via_hwloc(p_out)) {
            p_out.m_socket_count = 1;
            p_out.m_sockets[0].m_socket_id = 0;
            p_out.m_sockets[0].m_cpu_pct = 0.0f;
            p_out.m_sockets[0].m_mem_total_bytes = p_out.m_mem_total_bytes;
            p_out.m_sockets[0].m_mem_used_bytes = p_out.m_mem_total_bytes > p_out.m_mem_avail_bytes ? p_out.m_mem_total_bytes - p_out.m_mem_avail_bytes : 0;
        }

        p_out.m_net_aggregate = net_stats{};
        p_out.m_disk_aggregate = disk_stats{};
    }

} // namespace gempba::telemetry
