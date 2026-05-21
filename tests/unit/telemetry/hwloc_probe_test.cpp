#include <gempba/config.h>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/hwloc_probe.hpp>

#if GEMPBA_HWLOC
    #include <hwloc.h>
    #include <impl/telemetry/hwloc_probe_detail.hpp>
#endif

namespace {

#if GEMPBA_HWLOC

    // Constructs a deterministic synthetic hwloc topology per test. Tests target
    // the detail:: helpers directly so behaviour is fully decoupled from host
    // hardware, env vars, and hwloc's discovery-component fallback rules.
    class synthetic_topology_test : public ::testing::Test {
    protected:
        hwloc_topology_t m_topology = nullptr;

        void TearDown() override {
            if (m_topology != nullptr) {
                hwloc_topology_destroy(m_topology);
                m_topology = nullptr;
            }
        }

        void load_synthetic(const char* p_spec) {
            ASSERT_EQ(0, hwloc_topology_init(&m_topology));
            ASSERT_EQ(0, hwloc_topology_set_synthetic(m_topology, p_spec)) << "spec rejected by hwloc: " << p_spec;
            ASSERT_EQ(0, hwloc_topology_load(m_topology)) << "load failed for spec: " << p_spec;
        }
    };

    // ── fill_static_topology_from_topology branches ─────────────────────────

    TEST_F(synthetic_topology_test, fill_static_returns_false_when_no_packages) {
        load_synthetic("numanode:1 core:4 pu:2");
        gempba::telemetry::topology_node v_node;
        EXPECT_FALSE(gempba::telemetry::detail::fill_static_topology_from_topology(v_node, m_topology));
        EXPECT_TRUE(v_node.m_sockets.empty());
        EXPECT_EQ(0u, v_node.m_total_logical_cores);
        EXPECT_EQ(0u, v_node.m_total_physical_cores);
    }

    TEST_F(synthetic_topology_test, fill_static_dual_socket) {
        load_synthetic("pack:2 core:4 pu:2");
        gempba::telemetry::topology_node v_node;
        ASSERT_TRUE(gempba::telemetry::detail::fill_static_topology_from_topology(v_node, m_topology));

        EXPECT_EQ(8u, v_node.m_total_physical_cores);
        EXPECT_EQ(16u, v_node.m_total_logical_cores);
        ASSERT_EQ(2u, v_node.m_sockets.size());

        for (std::size_t v_i = 0; v_i < v_node.m_sockets.size(); ++v_i) {
            const auto& v_socket = v_node.m_sockets[v_i];
            EXPECT_EQ(static_cast<std::uint8_t>(v_i), v_socket.m_socket_id);
            EXPECT_EQ(4u, v_socket.m_physical_cores);
            EXPECT_EQ(8u, v_socket.m_logical_cores);
            EXPECT_EQ(8u, v_socket.m_cpu_ids.size());
        }
    }

    TEST_F(synthetic_topology_test, fill_static_single_socket) {
        load_synthetic("pack:1 core:8 pu:2");
        gempba::telemetry::topology_node v_node;
        ASSERT_TRUE(gempba::telemetry::detail::fill_static_topology_from_topology(v_node, m_topology));

        ASSERT_EQ(1u, v_node.m_sockets.size());
        EXPECT_EQ(8u, v_node.m_total_physical_cores);
        EXPECT_EQ(16u, v_node.m_total_logical_cores);
        EXPECT_EQ(8u, v_node.m_sockets.front().m_physical_cores);
        EXPECT_EQ(16u, v_node.m_sockets.front().m_logical_cores);
        EXPECT_EQ(16u, v_node.m_sockets.front().m_cpu_ids.size());
    }

    TEST_F(synthetic_topology_test, fill_static_synthetic_brand_string_empty) {
        load_synthetic("pack:1 core:1 pu:1");
        gempba::telemetry::topology_node v_node;
        ASSERT_TRUE(gempba::telemetry::detail::fill_static_topology_from_topology(v_node, m_topology));
        ASSERT_EQ(1u, v_node.m_sockets.size());
        EXPECT_TRUE(v_node.m_sockets.front().m_name.empty());
    }

    TEST_F(synthetic_topology_test, fill_static_cpu_ids_match_logical_count) {
        load_synthetic("pack:2 core:2 pu:2");
        gempba::telemetry::topology_node v_node;
        ASSERT_TRUE(gempba::telemetry::detail::fill_static_topology_from_topology(v_node, m_topology));
        for (const auto& v_socket: v_node.m_sockets) {
            EXPECT_EQ(static_cast<std::size_t>(v_socket.m_logical_cores), v_socket.m_cpu_ids.size());
        }
    }

    // ── fill_runtime_from_topology branches ─────────────────────────────────

    TEST_F(synthetic_topology_test, fill_runtime_returns_false_when_no_packages) {
        load_synthetic("numanode:1 core:4 pu:2");
        gempba::telemetry::node_frame v_frame{};
        EXPECT_FALSE(gempba::telemetry::detail::fill_runtime_from_topology(v_frame, m_topology));
        EXPECT_EQ(0u, v_frame.m_socket_count);
    }

    TEST_F(synthetic_topology_test, fill_runtime_dual_socket) {
        load_synthetic("pack:2 core:4 pu:2");
        gempba::telemetry::node_frame v_frame{};
        ASSERT_TRUE(gempba::telemetry::detail::fill_runtime_from_topology(v_frame, m_topology));
        EXPECT_EQ(2u, v_frame.m_socket_count);
        for (std::uint8_t v_i = 0; v_i < v_frame.m_socket_count; ++v_i) {
            EXPECT_EQ(v_i, v_frame.m_sockets[v_i].m_socket_id);
            EXPECT_EQ(0.0f, v_frame.m_sockets[v_i].m_cpu_pct);
            EXPECT_EQ(0u, v_frame.m_sockets[v_i].m_mem_used_bytes);
        }
    }

    TEST_F(synthetic_topology_test, fill_runtime_clamps_to_max_sockets) {
        // MAX_SOCKETS=8 — request more so the std::min clamp branch is exercised.
        load_synthetic("pack:16 core:1 pu:1");
        gempba::telemetry::node_frame v_frame{};
        ASSERT_TRUE(gempba::telemetry::detail::fill_runtime_from_topology(v_frame, m_topology));
        EXPECT_EQ(static_cast<std::uint8_t>(gempba::telemetry::MAX_SOCKETS), v_frame.m_socket_count);
    }

    TEST_F(synthetic_topology_test, fill_runtime_with_numa_per_socket_accumulates_memory) {
        // [numa] interleaves a NUMA node under each package, exercising both
        // the cpuset-isincluded true branch and the local_memory accumulation.
        load_synthetic("pack:2 [numa] core:2 pu:1");
        gempba::telemetry::node_frame v_frame{};
        ASSERT_TRUE(gempba::telemetry::detail::fill_runtime_from_topology(v_frame, m_topology));
        EXPECT_EQ(2u, v_frame.m_socket_count);
    }

#else // !GEMPBA_HWLOC

    TEST(hwloc_probe_off_stub_test, fill_static_returns_false) {
        gempba::telemetry::topology_node v_node;
        EXPECT_FALSE(gempba::telemetry::fill_static_topology_via_hwloc(v_node));
        EXPECT_TRUE(v_node.m_sockets.empty());
    }

    TEST(hwloc_probe_off_stub_test, fill_runtime_returns_false) {
        gempba::telemetry::node_frame v_frame{};
        EXPECT_FALSE(gempba::telemetry::fill_runtime_via_hwloc(v_frame));
        EXPECT_EQ(0u, v_frame.m_socket_count);
    }

#endif

} // namespace
