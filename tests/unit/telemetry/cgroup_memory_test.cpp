#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>
#include <impl/telemetry/cgroup_memory.hpp>

namespace {

    namespace fs = std::filesystem;
    using gempba::telemetry::detail::resolve_cgroup_memory;

    // ~755 GiB -- the test node's physical RAM, used as the "above this is
    // effectively unlimited" ceiling.
    constexpr std::uint64_t HOST_TOTAL = 811748818944ULL;
    // The cgroup-v1 token written for an unbounded memory limit.
    constexpr std::uint64_t V1_UNLIMITED = 9223372036854771712ULL;

    // Builds a throwaway cgroup-like directory tree on disk so the hierarchy walk
    // can be exercised on any platform: it is pure file I/O, and only the real
    // /sys/fs/cgroup paths are Linux-specific. Each test gets its own temp root.
    class cgroup_memory_test : public ::testing::Test {
    protected:
        void SetUp() override {
            std::error_code v_ec;
            const fs::path v_tmp = fs::temp_directory_path(v_ec);
            if (v_ec) {
                GTEST_SKIP() << "no usable temp directory: " << v_ec.message();
            }
            const std::string v_name = ::testing::UnitTest::GetInstance()->current_test_info()->name();
            m_root = (v_tmp / ("gempba_cgroup_" + v_name)).generic_string();
            fs::remove_all(m_root, v_ec);
        }

        void TearDown() override {
            std::error_code v_ec;
            fs::remove_all(m_root, v_ec);
        }

        // Writes p_value into <m_root>/p_rel, creating parent directories.
        void write(const std::string& p_rel, const std::string& p_value) const {
            const fs::path v_path = fs::path(m_root) / p_rel;
            fs::create_directories(v_path.parent_path());
            std::ofstream v_out(v_path);
            v_out << p_value;
        }

        // Absolute (forward-slash) path of <m_root>/p_rel.
        [[nodiscard]] std::string at(const std::string& p_rel) const { return (fs::path(m_root) / p_rel).generic_string(); }

        std::string m_root;
    };

    // The layout a cgroup-based workload manager produces on a shared node, with
    // the real byte values measured on a two-process node: the per-process leaf
    // is left unlimited and accounts a single process, while an ancestor carries
    // the 2 GiB allocation and accounts every process beneath it.
    TEST_F(cgroup_memory_test, uses_enforcing_ancestor_not_unlimited_leaf) {
        const std::string v_base = "system.slice/workload.scope/group/alloc";
        write(v_base + "/user/leaf/memory.max", "max");
        write(v_base + "/user/leaf/memory.current", "240312320");
        write(v_base + "/user/memory.max", "2147483648");
        write(v_base + "/user/memory.current", "490934272");
        write(v_base + "/memory.max", "max");
        write(v_base + "/memory.current", "519606272");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at(v_base + "/user/leaf"), m_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(490934272ULL, v_used);
        EXPECT_EQ(2147483648ULL, v_limit);
    }

    // The kernel's effective limit is the minimum over the chain. When more than
    // one ancestor carries a real limit, the tightest wins, and usage is read at
    // the cgroup that owns it -- not simply the first one found walking up.
    TEST_F(cgroup_memory_test, picks_tightest_limit_across_chain) {
        write("g/p/leaf/memory.max", "max");
        write("g/p/leaf/memory.current", "100");
        write("g/p/memory.max", "2147483648"); // 2 GiB on the closer ancestor
        write("g/p/memory.current", "200");
        write("g/memory.max", "1073741824"); // 1 GiB on the higher ancestor -- tighter
        write("g/memory.current", "300");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("g/p/leaf"), m_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(300ULL, v_used);
        EXPECT_EQ(1073741824ULL, v_limit);
    }

    // A limit at/above physical RAM is effectively unbounded: skip it and keep
    // walking up to a real allocation.
    TEST_F(cgroup_memory_test, skips_out_of_range_limit_and_keeps_walking) {
        write("top/mid/leaf/memory.max", "max");
        write("top/mid/leaf/memory.current", "111");
        write("top/mid/memory.max", std::to_string(V1_UNLIMITED)); // > host total -> skip
        write("top/mid/memory.current", "222");
        write("top/memory.max", "2147483648"); // 2 GiB, the real allocation
        write("top/memory.current", "333");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("top/mid/leaf"), m_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(333ULL, v_used);
        EXPECT_EQ(2147483648ULL, v_limit);
    }

    // No real limit anywhere up the chain: report no limit and the leaf's own
    // usage, so the consumer falls back to host memory.
    TEST_F(cgroup_memory_test, no_enforced_limit_reports_leaf_usage_and_zero_limit) {
        write("a/b/c/memory.max", "max");
        write("a/b/c/memory.current", "111");
        write("a/b/memory.max", "max");
        write("a/b/memory.current", "222");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("a/b/c"), m_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(111ULL, v_used);
        EXPECT_EQ(0ULL, v_limit);
    }

    // The walk stops at p_root: a limit on an ancestor *above* the search root
    // must never be used.
    TEST_F(cgroup_memory_test, does_not_walk_above_root) {
        write("top/memory.max", "1073741824"); // 1 GiB, but above the search root
        write("top/mid/memory.max", "max");
        write("top/mid/leaf/memory.max", "max");
        write("top/mid/leaf/memory.current", "555");

        const std::string v_root = at("top/mid");
        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("top/mid/leaf"), v_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(555ULL, v_used);
        EXPECT_EQ(0ULL, v_limit);
    }

    // Filename-agnostic: the same walk serves cgroup-v1 file names.
    TEST_F(cgroup_memory_test, works_with_v1_filenames) {
        write("job/task/memory.limit_in_bytes", std::to_string(V1_UNLIMITED));
        write("job/task/memory.usage_in_bytes", "100");
        write("job/memory.limit_in_bytes", "2147483648");
        write("job/memory.usage_in_bytes", "777");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("job/task"), m_root, "memory.limit_in_bytes", "memory.usage_in_bytes", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(777ULL, v_used);
        EXPECT_EQ(2147483648ULL, v_limit);
    }

    // A zero host total disables the ceiling check: any real limit is accepted.
    TEST_F(cgroup_memory_test, zero_host_total_accepts_any_real_limit) {
        write("g/memory.max", "4096");
        write("g/memory.current", "1024");

        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("g"), m_root, "memory.max", "memory.current", /*host_total*/ 0, v_used, v_limit);

        EXPECT_EQ(1024ULL, v_used);
        EXPECT_EQ(4096ULL, v_limit);
    }

    // A missing tree yields zeros -- the safe "no cgroup data" signal.
    TEST_F(cgroup_memory_test, missing_tree_reports_zero) {
        std::uint64_t v_used = 0, v_limit = 0;
        resolve_cgroup_memory(at("does/not/exist"), m_root, "memory.max", "memory.current", HOST_TOTAL, v_used, v_limit);

        EXPECT_EQ(0ULL, v_used);
        EXPECT_EQ(0ULL, v_limit);
    }

} // namespace
