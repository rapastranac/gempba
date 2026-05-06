#include <impl/telemetry/process_probe.hpp>

#include <chrono>

#if defined(_WIN32)
// psapi.h depends on Win32 types defined by windows.h, so order is significant.
// clang-format off
    #include <windows.h>
    #include <psapi.h>
// clang-format on
#elif defined(__APPLE__)
    #include <mach/mach.h>
    #include <sys/resource.h>
#else
    #include <fstream>
    #include <string>
    #include <sys/resource.h>
    #include <unistd.h>
#endif

namespace gempba::telemetry {

    namespace {

        std::uint64_t monotonic_microseconds() noexcept {
            using clock = std::chrono::steady_clock;
            return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count());
        }

#if defined(_WIN32)

        std::uint64_t filetime_to_microseconds(const FILETIME& p_ft) noexcept {
            ULARGE_INTEGER v_ull;
            v_ull.LowPart = p_ft.dwLowDateTime;
            v_ull.HighPart = p_ft.dwHighDateTime;
            return v_ull.QuadPart / 10ULL; // 100ns ticks → microseconds
        }

        void sample_platform(std::uint64_t& p_cpu_microseconds_out, std::uint64_t& p_rss_bytes_out) noexcept {
            FILETIME v_creation, v_exit, v_kernel, v_user;
            if (GetProcessTimes(GetCurrentProcess(), &v_creation, &v_exit, &v_kernel, &v_user)) {
                p_cpu_microseconds_out = filetime_to_microseconds(v_kernel) + filetime_to_microseconds(v_user);
            } else {
                p_cpu_microseconds_out = 0;
            }

            PROCESS_MEMORY_COUNTERS v_pmc{};
            if (GetProcessMemoryInfo(GetCurrentProcess(), &v_pmc, sizeof(v_pmc))) {
                p_rss_bytes_out = static_cast<std::uint64_t>(v_pmc.WorkingSetSize);
            } else {
                p_rss_bytes_out = 0;
            }
        }

#else

        std::uint64_t timeval_to_microseconds(const struct timeval& p_tv) noexcept {
            return static_cast<std::uint64_t>(p_tv.tv_sec) * 1'000'000ULL + static_cast<std::uint64_t>(p_tv.tv_usec);
        }

    #if defined(__APPLE__)

        // RSS via Mach: portable across Intel and Apple Silicon, no /proc.
        std::uint64_t read_rss_bytes() noexcept {
            mach_task_basic_info_data_t v_info{};
            mach_msg_type_number_t v_count = MACH_TASK_BASIC_INFO_COUNT;
            if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&v_info), &v_count) == KERN_SUCCESS) {
                return static_cast<std::uint64_t>(v_info.resident_size);
            }
            return 0;
        }

    #else

        std::uint64_t read_rss_bytes() noexcept {
            std::ifstream v_status("/proc/self/status");
            if (!v_status)
                return 0;
            std::string v_line;
            while (std::getline(v_status, v_line)) {
                if (v_line.rfind("VmRSS:", 0) == 0) {
                    const auto v_pos = v_line.find_first_of("0123456789");
                    if (v_pos != std::string::npos) {
                        return static_cast<std::uint64_t>(std::stoull(v_line.substr(v_pos))) * 1024ULL;
                    }
                }
            }
            return 0;
        }

    #endif

        void sample_platform(std::uint64_t& p_cpu_microseconds_out, std::uint64_t& p_rss_bytes_out) noexcept {
            struct rusage v_ru{};
            if (getrusage(RUSAGE_SELF, &v_ru) == 0) {
                p_cpu_microseconds_out = timeval_to_microseconds(v_ru.ru_utime) + timeval_to_microseconds(v_ru.ru_stime);
            } else {
                p_cpu_microseconds_out = 0;
            }
            p_rss_bytes_out = read_rss_bytes();
        }

#endif

    } // namespace

    process_probe::process_probe() noexcept = default;

    void process_probe::sample(worker_frame& p_out) noexcept {
        std::uint64_t v_cpu_microseconds = 0;
        std::uint64_t v_rss_bytes = 0;
        sample_platform(v_cpu_microseconds, v_rss_bytes);

        const std::uint64_t v_wall_microseconds = monotonic_microseconds();

        if (m_has_baseline) {
            const std::uint64_t v_cpu_delta = v_cpu_microseconds - m_last_cpu_microseconds;
            const std::uint64_t v_wall_delta = v_wall_microseconds - m_last_wall_microseconds;
            p_out.m_process_cpu_pct = v_wall_delta > 0 ? static_cast<float>(static_cast<double>(v_cpu_delta) * 100.0 / static_cast<double>(v_wall_delta)) : 0.0f;
        } else {
            p_out.m_process_cpu_pct = 0.0f;
            m_has_baseline = true;
        }
        m_last_cpu_microseconds = v_cpu_microseconds;
        m_last_wall_microseconds = v_wall_microseconds;

        p_out.m_process_rss_bytes = v_rss_bytes;
    }

} // namespace gempba::telemetry
