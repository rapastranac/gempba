#ifndef GEMPBA_CENTRALIZED_UTILS_HPP
#define GEMPBA_CENTRALIZED_UTILS_HPP
#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>

/*
 * Author:  David Robert Nadeau
 * Site:    http://NadeauSoftware.com/
 * License: Creative Commons Attribution 3.0 Unported License
 *          http://creativecommons.org/licenses/by/3.0/deed.en_US
 */

#if defined(_WIN32)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #define RPC_NO_WINDOWS_H
    #include <psapi.h>
    #include <windows.h>

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))

    #include <sys/resource.h>
    #include <unistd.h>

    #if defined(__APPLE__) && defined(__MACH__)
        #include <mach/mach.h>

    #elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
        #include <fcntl.h>
        #include <procfs.h>

    #elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)

        #include <cstdio>

    #endif

#else
    #error "Cannot define getPeakRSS( ) or getCurrentRSS( ) for an unknown OS."
#endif

#include <utility>


/**
  Return number of bits that are set to 1
**/
inline int get_nb_set_bits(const char p_c) {
    // all credits to https://stackoverflow.com/questions/697978/c-code-to-count-the-number-of-1-bits-in-an-unsigned-char
    return static_cast<int>((p_c * 01001001001ULL & 042104210421ULL) % 017);
}

inline int get_nb_set_bits(const gempba::task_packet &p_task) {
    int v_nb = 0;
    for (const std::byte &v_byte: p_task) {
        v_nb += get_nb_set_bits(static_cast<char>(v_byte));
    }
    return v_nb;
}

class task_comparator {
public:
    bool operator()(const gempba::task_packet &p_t1, const gempba::task_packet &p_t2) const {

        const int v_n1 = get_nb_set_bits(p_t1);
        const int v_n2 = get_nb_set_bits(p_t2);

        return (v_n1 <= v_n2);
    }
};

inline int get_nb_set_bits(const gempba::task_bundle &p_bundle) {
    const gempba::task_packet &v_packet = p_bundle.get_task_packet();
    const int v_nb_set_bits = get_nb_set_bits(v_packet);

    const int v_runnable_id = p_bundle.get_runnable_id();
    const int v_nb = v_nb_set_bits + get_nb_set_bits(static_cast<char>(v_runnable_id));

    return v_nb;
}

class task_bundle_comparator {
public:
    bool operator()(const gempba::task_bundle &p_bundle1, const gempba::task_bundle &p_bundle2) const {

        const int v_n1 = get_nb_set_bits(p_bundle1);
        const int v_n2 = get_nb_set_bits(p_bundle2);

        return (v_n1 <= v_n2);
    }
};

/**
 * Returns the peak (maximum so far) resident set size (physical
 * memory use) measured in bytes, or zero if the value cannot be
 * determined on this OS.
 */
inline size_t get_peak_rss() {
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS v_info;
    GetProcessMemoryInfo(GetCurrentProcess(), &v_info, sizeof(v_info));
    return (size_t) v_info.PeakWorkingSetSize;

#elif (defined(_AIX) || defined(__TOS__AIX__)) || (defined(__sun__) || defined(__sun) || defined(sun) && (defined(__SVR4) || defined(__svr4__)))
    /* AIX and Solaris ------------------------------------------ */
    struct psinfo psinfo;
    int fd = -1;
    if ((fd = open("/proc/self/psinfo", O_RDONLY)) == -1)
        return (size_t) 0L; /* Can't open? */
    if (read(fd, &psinfo, sizeof(psinfo)) != sizeof(psinfo)) {
        close(fd);
        return (size_t) 0L; /* Can't read? */
    }
    close(fd);
    return (size_t) (psinfo.pr_rssize * 1024L);

#elif defined(__unix__) || defined(__unix) || defined(unix) || (defined(__APPLE__) && defined(__MACH__))
    /* BSD, Linux, and OSX -------------------------------------- */
    struct rusage v_rusage{};
    getrusage(RUSAGE_SELF, &v_rusage);
    #if defined(__APPLE__) && defined(__MACH__)
    return (size_t) v_rusage.ru_maxrss;
    #else
    return (size_t) (v_rusage.ru_maxrss * 1024L);
    #endif

#else
    /* Unknown OS ----------------------------------------------- */
    return (size_t) 0L; /* Unsupported. */
#endif
}

/**
 * Returns the current resident set size (physical memory use) measured
 * in bytes, or zero if the value cannot be determined on this OS.
 */
inline size_t get_current_rss() {
#if defined(_WIN32)
    /* Windows -------------------------------------------------- */
    PROCESS_MEMORY_COUNTERS v_info;
    GetProcessMemoryInfo(GetCurrentProcess(), &v_info, sizeof(v_info));
    return (size_t) v_info.WorkingSetSize;

#elif defined(__APPLE__) && defined(__MACH__)
    /* OSX ------------------------------------------------------ */
    struct mach_task_basic_info info;
    mach_msg_type_number_t infoCount = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t) &info, &infoCount) != KERN_SUCCESS)
        return (size_t) 0L; /* Can't access? */
    return (size_t) info.resident_size;

#elif defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
    /* Linux ---------------------------------------------------- */
    long v_rss = 0L;
    FILE *v_fp = nullptr;
    if ((v_fp = fopen("/proc/self/statm", "r")) == nullptr)
        return (size_t) 0L; /* Can't open? */
    if (fscanf(v_fp, "%*s%ld", &v_rss) != 1) { // NOLINT(cert-err34-c)
        fclose(v_fp);
        return (size_t) 0L; /* Can't read? */
    }
    fclose(v_fp);
    return (size_t) v_rss * (size_t) sysconf(_SC_PAGESIZE);

#else
    /* AIX, BSD, Solaris, and Unknown OS ------------------------ */
    return (size_t) 0L; /* Unsupported. */
#endif
}

#endif // GEMPBA_CENTRALIZED_UTILS_HPP
