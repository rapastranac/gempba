#ifndef GEMPBA_TELEMETRY_CGROUP_MEMORY_HPP
#define GEMPBA_TELEMETRY_CGROUP_MEMORY_HPP

#include <cstdint>
#include <fstream>
#include <string>

namespace gempba::telemetry::detail {

    /**
     * @brief First whitespace-delimited unsigned integer in @p p_path, or 0 when
     * the file is missing, empty, or holds the literal "max" (the cgroup-v2 token
     * for "no limit"). Never throws.
     */
    inline std::uint64_t read_cgroup_uint(const std::string& p_path) noexcept {
        std::ifstream v_in(p_path);
        if (!v_in)
            return 0;
        std::string v_token;
        v_in >> v_token;
        if (v_token.empty() || v_token == "max")
            return 0;
        try {
            return static_cast<std::uint64_t>(std::stoull(v_token));
        } catch (...) {
            return 0;
        }
    }

    /**
     * @brief Resolve a process's memory-cgroup usage and limit by walking from its
     * leaf cgroup @p p_dir up to the hierarchy root @p p_root.
     *
     * Schedulers enforce a job's memory allocation on an ancestor of the process's
     * leaf cgroup and leave the leaf itself unlimited ("max"). The kernel's
     * effective limit is the smallest real limit over the chain, so we take that
     * minimum (ignoring "max" and any value above physical RAM @p p_host_total) and
     * read usage at the deepest cgroup carrying it, which counts every process the
     * limit binds. With no real limit anywhere, report 0 so the consumer falls back
     * to host-wide memory.
     *
     * The limit/usage filenames are parameters, so one walk serves cgroup v2
     * (memory.max / memory.current) and v1 (memory.limit_in_bytes /
     * memory.usage_in_bytes). Precondition: @p p_dir is @p p_root or below it, '/'
     * separated. Never throws.
     */
    inline void resolve_cgroup_memory(std::string p_dir, const std::string& p_root, const char* p_limit_file, const char* p_used_file, std::uint64_t p_host_total, std::uint64_t& p_used,
                                      std::uint64_t& p_limit) noexcept {
        const std::string v_leaf = p_dir;
        std::uint64_t v_best_limit = 0; // 0 == no real limit seen yet
        std::uint64_t v_best_used = 0;
        for (;;) {
            const std::uint64_t v_limit = read_cgroup_uint(p_dir + "/" + p_limit_file);
            if (v_limit != 0 && (p_host_total == 0 || v_limit <= p_host_total)) {
                // Strictly-smaller keeps the deepest cgroup carrying the minimum,
                // since the walk visits leaf-first; its usage is the tightest
                // accounting of the processes the limit actually binds.
                if (v_best_limit == 0 || v_limit < v_best_limit) {
                    v_best_limit = v_limit;
                    v_best_used = read_cgroup_uint(p_dir + "/" + p_used_file);
                }
            }
            if (p_dir.size() <= p_root.size())
                break;
            const auto v_slash = p_dir.find_last_of('/');
            if (v_slash == std::string::npos || v_slash < p_root.size())
                break;
            p_dir.resize(v_slash);
        }
        p_limit = v_best_limit;
        p_used = v_best_limit != 0 ? v_best_used : read_cgroup_uint(v_leaf + "/" + p_used_file);
    }

} // namespace gempba::telemetry::detail

#endif // GEMPBA_TELEMETRY_CGROUP_MEMORY_HPP
