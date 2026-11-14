#include <chrono>
#include <cstdint>
#include <cstring>
#include <gempba/config.h>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/node_probe.hpp>
#include <thread>

#if GEMPBA_HWLOC
    #include <impl/telemetry/hwloc_probe_detail.hpp>
#endif

namespace {

    TEST(node_probe_test, sample_fills_hostname) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_GT(std::strlen(v_frame.m_hostname), 0u);
    }

    TEST(node_probe_test, sample_reports_total_memory) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_GT(v_frame.m_mem_total_bytes, 0u);
        EXPECT_LE(v_frame.m_mem_avail_bytes, v_frame.m_mem_total_bytes);
    }

    TEST(node_probe_test, sample_emits_at_least_one_socket) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_GE(v_frame.m_socket_count, 1u);
    }

    TEST(node_probe_test, sample_reports_at_least_one_logical_core) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_GE(v_frame.m_logical_cores, 1u);
    }

    TEST(node_probe_test, later_samples_report_the_sockets_of_the_first) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_first{};
        gempba::telemetry::node_frame v_later{};
        v_probe.sample(v_first);
        v_probe.sample(v_later);
        ASSERT_EQ(v_first.m_socket_count, v_later.m_socket_count);
        for (std::uint8_t v_i = 0; v_i < v_first.m_socket_count; ++v_i) {
            EXPECT_EQ(v_first.m_sockets[v_i].m_socket_id, v_later.m_sockets[v_i].m_socket_id);
            EXPECT_EQ(v_first.m_sockets[v_i].m_mem_total_bytes, v_later.m_sockets[v_i].m_mem_total_bytes);
        }
    }

#if GEMPBA_HWLOC
    TEST(node_probe_test, sample_loads_the_host_topology_on_the_first_frame_only) {
        gempba::telemetry::node_probe v_probe;
        gempba::telemetry::node_frame v_frame{};
        const std::uint64_t v_before = gempba::telemetry::detail::host_topology_loads();
        v_probe.sample(v_frame);
        const std::uint64_t v_after_first = gempba::telemetry::detail::host_topology_loads();
        for (int v_i = 0; v_i < 3; ++v_i) {
            v_probe.sample(v_frame);
        }
        EXPECT_EQ(v_before + 1, v_after_first);
        EXPECT_EQ(v_after_first, gempba::telemetry::detail::host_topology_loads());
    }
#endif

    class node_emit_test : public ::testing::Test {
    protected:
        void TearDown() override { gempba::telemetry::uninstall(); }
    };

    TEST_F(node_emit_test, sentinel_publishes_node_frame_via_pump) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        v_hub.set_interval_ms(50);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        const auto v_node = v_hub.latest_node_frame(0);
        ASSERT_TRUE(v_node.has_value());
        EXPECT_EQ(0u, v_node->m_sentinel_rank);
        EXPECT_GT(v_node->m_mem_total_bytes, 0u);
    }

} // namespace
