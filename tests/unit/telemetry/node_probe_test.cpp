#include <chrono>
#include <cstring>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/node_probe.hpp>
#include <thread>

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

    class node_emit_test : public ::testing::Test {
    protected:
        void TearDown() override { gempba::telemetry::uninstall(); }
    };

    TEST_F(node_emit_test, sentinel_publishes_node_frame_via_pump) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        v_hub.set_worker_interval_ms(500);
        v_hub.set_node_interval_ms(50);

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        const auto v_node = v_hub.latest_node_frame(0);
        ASSERT_TRUE(v_node.has_value());
        EXPECT_EQ(0u, v_node->m_sentinel_worker_id);
        EXPECT_GT(v_node->m_mem_total_bytes, 0u);
    }

} // namespace
