#include <cstring>
#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/topology_builder.hpp>

namespace {

    TEST(topology_builder_test, mt_only_self_registers_one_node_one_worker) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);

        ASSERT_EQ(1u, v_snap.m_identities.size());
        ASSERT_EQ(1u, v_snap.m_nodes.size());

        EXPECT_EQ(0u, v_snap.m_identities.front().m_worker_id);
        EXPECT_GT(v_snap.m_identities.front().m_pid, 0u);

        EXPECT_EQ(1u, v_snap.m_nodes.front().m_worker_ids.size());
        EXPECT_EQ(0u, v_snap.m_nodes.front().m_worker_ids.front());
        EXPECT_EQ(0u, v_snap.m_nodes.front().m_sentinel_worker_id);
    }

    TEST(topology_builder_test, hostname_field_is_non_empty) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        ASSERT_FALSE(v_snap.m_nodes.empty());
        EXPECT_FALSE(v_snap.m_nodes.front().m_hostname.empty());
        EXPECT_GT(std::strlen(v_snap.m_identities.front().m_hostname), 0u);
    }

    TEST(topology_builder_test, custom_worker_id_carried_through) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 7, 1);
        ASSERT_FALSE(v_snap.m_identities.empty());
        EXPECT_EQ(7u, v_snap.m_identities.front().m_worker_id);
        EXPECT_EQ(7u, v_snap.m_nodes.front().m_sentinel_worker_id);
    }

    TEST(topology_builder_test, hub_exposes_topology_after_runtime_ready) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        const auto& v_topo = v_hub.current_topology();
        ASSERT_EQ(1u, v_topo.m_nodes.size());
        EXPECT_EQ(1u, v_topo.m_nodes.front().m_worker_ids.size());
    }

    TEST(topology_builder_test, node_carries_logical_cores_and_memory) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        const auto& v_node = v_snap.m_nodes.front();
        EXPECT_GE(v_node.m_total_logical_cores, 1u);
        EXPECT_GT(v_node.m_mem_total_bytes, 0u);
    }

    TEST(topology_builder_test, node_has_one_synthetic_socket_with_cpu_ids) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        const auto& v_node = v_snap.m_nodes.front();
        ASSERT_EQ(1u, v_node.m_sockets.size());
        const auto& v_sock = v_node.m_sockets.front();
        EXPECT_EQ(0u, v_sock.m_socket_id);
        EXPECT_EQ(v_node.m_total_logical_cores, v_sock.m_logical_cores);
        EXPECT_EQ(static_cast<std::size_t>(v_sock.m_logical_cores), v_sock.m_cpu_ids.size());
        for (std::size_t v_i = 0; v_i < v_sock.m_cpu_ids.size(); ++v_i) {
            EXPECT_EQ(static_cast<std::uint32_t>(v_i), v_sock.m_cpu_ids[v_i]);
        }
    }

    TEST(topology_builder_test, identity_affinity_mask_has_at_least_one_bit_set) {
        const auto v_snap = gempba::telemetry::build_topology_snapshot(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        EXPECT_NE(0u, v_snap.m_identities.front().m_allowed_cpu_mask);
    }

} // namespace
