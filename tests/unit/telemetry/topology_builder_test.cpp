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

    // ── per-node topology gather (serialize / deserialize) ─────────────────────

    TEST(topology_builder_test, serialize_round_trips_wide_sockets) {
        using namespace gempba::telemetry;
        topology_node v_node;
        v_node.m_hostname = "fc30557";
        v_node.m_total_physical_cores = 192;
        v_node.m_total_logical_cores = 192;
        v_node.m_mem_total_bytes = 755ULL * 1024 * 1024 * 1024;
        // Two 96-core EPYC sockets -- 96 cpu_ids each, well past any fixed cap.
        for (std::uint8_t v_s = 0; v_s < 2; ++v_s) {
            topology_socket v_socket;
            v_socket.m_socket_id = v_s;
            v_socket.m_name = "AMD EPYC 9655 96-Core Processor";
            v_socket.m_physical_cores = 96;
            v_socket.m_logical_cores = 96;
            for (std::uint32_t v_c = 0; v_c < 96; ++v_c) {
                v_socket.m_cpu_ids.push_back(static_cast<std::uint32_t>(v_s) * 96u + v_c);
            }
            v_node.m_sockets.push_back(std::move(v_socket));
        }

        std::vector<std::uint8_t> v_blob;
        detail::serialize_node_topology(v_blob, v_node);

        topology_node v_back;
        ASSERT_TRUE(detail::deserialize_node_topology(v_back, v_blob.data(), v_blob.size()));
        EXPECT_EQ("fc30557", v_back.m_hostname);
        EXPECT_EQ(192u, v_back.m_total_logical_cores);
        EXPECT_EQ(v_node.m_mem_total_bytes, v_back.m_mem_total_bytes);
        ASSERT_EQ(2u, v_back.m_sockets.size());
        EXPECT_EQ("AMD EPYC 9655 96-Core Processor", v_back.m_sockets[1].m_name);
        ASSERT_EQ(96u, v_back.m_sockets[0].m_cpu_ids.size());
        ASSERT_EQ(96u, v_back.m_sockets[1].m_cpu_ids.size());
        EXPECT_EQ(0u, v_back.m_sockets[0].m_cpu_ids.front());
        EXPECT_EQ(95u, v_back.m_sockets[0].m_cpu_ids.back());
        EXPECT_EQ(96u, v_back.m_sockets[1].m_cpu_ids.front());
        EXPECT_EQ(191u, v_back.m_sockets[1].m_cpu_ids.back());
    }

    TEST(topology_builder_test, serialize_round_trips_node_with_no_sockets) {
        using namespace gempba::telemetry;
        topology_node v_node;
        v_node.m_hostname = "blank";

        std::vector<std::uint8_t> v_blob;
        detail::serialize_node_topology(v_blob, v_node);

        topology_node v_back;
        v_back.m_sockets.emplace_back(); // a pre-existing socket must be cleared
        ASSERT_TRUE(detail::deserialize_node_topology(v_back, v_blob.data(), v_blob.size()));
        EXPECT_EQ("blank", v_back.m_hostname);
        EXPECT_TRUE(v_back.m_sockets.empty());
    }

    TEST(topology_builder_test, deserialize_rejects_truncated_buffer) {
        using namespace gempba::telemetry;
        topology_node v_node;
        v_node.m_hostname = "fc1";
        topology_socket v_socket;
        v_socket.m_cpu_ids = {1, 2, 3, 4};
        v_node.m_sockets.push_back(std::move(v_socket));

        std::vector<std::uint8_t> v_blob;
        detail::serialize_node_topology(v_blob, v_node);
        v_blob.resize(v_blob.size() - 4); // chop the last cpu_id

        topology_node v_back;
        EXPECT_FALSE(detail::deserialize_node_topology(v_back, v_blob.data(), v_blob.size()));
    }

} // namespace
