#include <gempba/telemetry/frames.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/local_transport.hpp>
#include <vector>

namespace {

    TEST(local_transport_test, publish_then_poll_delivers_worker_frame) {
        gempba::telemetry::local_transport v_transport;

        gempba::telemetry::worker_frame v_in{};
        v_in.m_version = gempba::telemetry::TELEMETRY_SCHEMA_VERSION;
        v_in.m_worker_id = 3;
        v_in.m_tasks_local_total = 42;
        v_transport.publish_worker(v_in);

        std::vector<gempba::telemetry::worker_frame> v_received;
        v_transport.poll([&](std::uint32_t, const gempba::telemetry::worker_frame& p_f) { v_received.push_back(p_f); }, [&](std::uint32_t, const gempba::telemetry::node_frame&) {});

        ASSERT_EQ(1u, v_received.size());
        EXPECT_EQ(3u, v_received.front().m_worker_id);
        EXPECT_EQ(42u, v_received.front().m_tasks_local_total);
    }

    TEST(local_transport_test, poll_clears_dirty_so_subsequent_polls_skip) {
        gempba::telemetry::local_transport v_transport;
        gempba::telemetry::worker_frame v_in{};
        v_in.m_worker_id = 1;
        v_transport.publish_worker(v_in);

        int v_first_count = 0;
        v_transport.poll([&](std::uint32_t, const gempba::telemetry::worker_frame&) { ++v_first_count; }, [&](std::uint32_t, const gempba::telemetry::node_frame&) {});
        EXPECT_EQ(1, v_first_count);

        int v_second_count = 0;
        v_transport.poll([&](std::uint32_t, const gempba::telemetry::worker_frame&) { ++v_second_count; }, [&](std::uint32_t, const gempba::telemetry::node_frame&) {});
        EXPECT_EQ(0, v_second_count);
    }

    TEST(local_transport_test, latest_publish_replaces_previous) {
        gempba::telemetry::local_transport v_transport;

        gempba::telemetry::worker_frame v_first{};
        v_first.m_worker_id = 0;
        v_first.m_tasks_local_total = 1;
        v_transport.publish_worker(v_first);

        gempba::telemetry::worker_frame v_second{};
        v_second.m_worker_id = 0;
        v_second.m_tasks_local_total = 99;
        v_transport.publish_worker(v_second);

        std::uint64_t v_seen = 0;
        v_transport.poll([&](std::uint32_t, const gempba::telemetry::worker_frame& p_f) { v_seen = p_f.m_tasks_local_total; }, [&](std::uint32_t, const gempba::telemetry::node_frame&) {});
        EXPECT_EQ(99u, v_seen);
    }

    TEST(local_transport_test, control_messages_round_trip) {
        gempba::telemetry::local_transport v_transport;

        gempba::telemetry::control_msg v_msg{};
        v_msg.m_version = gempba::telemetry::TELEMETRY_SCHEMA_VERSION;
        v_msg.m_kind = gempba::telemetry::control_kind::SET_WORKER_INTERVAL_MS;
        v_msg.m_value = 250;

        v_transport.send_control(0, v_msg);

        const auto v_received = v_transport.try_recv_control();
        ASSERT_TRUE(v_received.has_value());
        EXPECT_EQ(gempba::telemetry::control_kind::SET_WORKER_INTERVAL_MS, v_received->m_kind);
        EXPECT_EQ(250u, v_received->m_value);

        EXPECT_FALSE(v_transport.try_recv_control().has_value());
    }

    TEST(local_transport_test, node_frame_round_trip) {
        gempba::telemetry::local_transport v_transport;

        gempba::telemetry::node_frame v_in{};
        v_in.m_version = gempba::telemetry::TELEMETRY_SCHEMA_VERSION;
        v_in.m_sentinel_worker_id = 0;
        v_in.m_socket_count = 2;
        v_in.m_mem_total_bytes = 1024 * 1024 * 1024;
        v_transport.publish_node(v_in);

        gempba::telemetry::node_frame v_out{};
        v_transport.poll([&](std::uint32_t, const gempba::telemetry::worker_frame&) {}, [&](std::uint32_t, const gempba::telemetry::node_frame& p_f) { v_out = p_f; });

        EXPECT_EQ(0u, v_out.m_sentinel_worker_id);
        EXPECT_EQ(2u, v_out.m_socket_count);
        EXPECT_EQ(1024ull * 1024 * 1024, v_out.m_mem_total_bytes);
    }

} // namespace
