#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gempba/telemetry/runtime_mode.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/local_transport.hpp>
#include <impl/telemetry/telemetry_transport.hpp>
#include <impl/telemetry/topology_builder.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {

    gempba::telemetry::topology_snapshot one_rank_snapshot(const std::uint32_t p_rank, const gempba::rank_role p_role = gempba::rank_role::WORKER) {
        gempba::telemetry::local_transport v_transport;
        return gempba::telemetry::build_topology_snapshot(v_transport, p_rank, 1, p_role);
    }

    /** @brief A transport whose all_gather calls return prepared replies in order, recording each block this rank sent. */
    class scripted_transport final : public gempba::telemetry::telemetry_transport {
    public:
        explicit scripted_transport(std::vector<std::vector<std::vector<std::byte>>> p_replies) : m_replies(std::move(p_replies)) {}

        void publish_rank(const gempba::telemetry::rank_frame&) override {}
        void publish_node(const gempba::telemetry::node_frame&) override {}
        void poll(const gempba::telemetry::rank_frame_sink&, const gempba::telemetry::node_frame_sink&) override {}
        void send_control(std::uint32_t, const gempba::telemetry::control_msg&) override {}
        std::optional<gempba::telemetry::control_msg> try_recv_control() override { return std::nullopt; }

        std::vector<std::vector<std::byte>> all_gather(const std::span<const std::byte> p_block) override {
            m_sent.emplace_back(p_block.begin(), p_block.end());
            return m_replies.at(m_sent.size() - 1);
        }

        std::vector<std::vector<std::byte>> m_sent;

    private:
        std::vector<std::vector<std::vector<std::byte>>> m_replies;
    };

    std::vector<std::byte> identity_block(const std::uint32_t p_rank, const std::string& p_hostname) {
        gempba::telemetry::rank_identity v_identity{};
        v_identity.m_rank = p_rank;
        std::strncpy(v_identity.m_hostname, p_hostname.c_str(), sizeof(v_identity.m_hostname) - 1);
        const auto v_bytes = std::as_bytes(std::span(&v_identity, 1));
        return {v_bytes.begin(), v_bytes.end()};
    }

    std::vector<std::byte> layout_block(const std::string& p_hostname, const std::uint16_t p_logical_cores) {
        gempba::telemetry::topology_node v_node;
        v_node.m_hostname = p_hostname;
        v_node.m_total_logical_cores = p_logical_cores;
        std::vector<std::uint8_t> v_bytes;
        gempba::telemetry::detail::serialize_node_topology(v_bytes, v_node);
        const auto v_view = std::as_bytes(std::span(v_bytes));
        return {v_view.begin(), v_view.end()};
    }

    TEST(topology_builder_test, mt_only_self_registers_one_node_one_rank) {
        const auto v_snap = one_rank_snapshot(0);

        ASSERT_EQ(1u, v_snap.m_identities.size());
        ASSERT_EQ(1u, v_snap.m_nodes.size());

        EXPECT_EQ(0u, v_snap.m_identities.front().m_rank);
        EXPECT_GT(v_snap.m_identities.front().m_pid, 0u);

        EXPECT_EQ(1u, v_snap.m_nodes.front().m_ranks.size());
        EXPECT_EQ(0u, v_snap.m_nodes.front().m_ranks.front());
        EXPECT_EQ(0u, v_snap.m_nodes.front().m_sentinel_rank);
    }

    TEST(topology_builder_test, hostname_field_is_non_empty) {
        const auto v_snap = one_rank_snapshot(0);
        ASSERT_FALSE(v_snap.m_nodes.empty());
        EXPECT_FALSE(v_snap.m_nodes.front().m_hostname.empty());
        EXPECT_GT(std::strlen(v_snap.m_identities.front().m_hostname), 0u);
    }

    TEST(topology_builder_test, custom_rank_carried_through) {
        const auto v_snap = one_rank_snapshot(7);
        ASSERT_FALSE(v_snap.m_identities.empty());
        EXPECT_EQ(7u, v_snap.m_identities.front().m_rank);
        EXPECT_EQ(7u, v_snap.m_nodes.front().m_sentinel_rank);
    }

    TEST(topology_builder_test, the_identity_carries_the_ranks_own_role) {
        const auto v_worker = one_rank_snapshot(0);
        ASSERT_FALSE(v_worker.m_identities.empty());
        EXPECT_EQ(gempba::rank_role::WORKER, v_worker.m_identities.front().m_role);

        const auto v_coordinator = one_rank_snapshot(0, gempba::rank_role::COORDINATOR);
        ASSERT_FALSE(v_coordinator.m_identities.empty());
        EXPECT_EQ(gempba::rank_role::COORDINATOR, v_coordinator.m_identities.front().m_role);
    }

    TEST(topology_builder_test, gathered_ranks_group_by_host_under_their_lowest_rank) {
        scripted_transport v_transport({
                {identity_block(0, "alpha"), identity_block(1, "beta"), identity_block(2, "alpha")},
                {layout_block("alpha", 8), layout_block("beta", 4), {}},
        });
        const auto v_snap = gempba::telemetry::build_topology_snapshot(v_transport, 1, 3);

        ASSERT_EQ(3u, v_snap.m_identities.size());
        ASSERT_EQ(2u, v_snap.m_nodes.size());
        EXPECT_EQ("alpha", v_snap.m_nodes[0].m_hostname);
        EXPECT_EQ((std::vector<std::uint32_t>{0, 2}), v_snap.m_nodes[0].m_ranks);
        EXPECT_EQ(0u, v_snap.m_nodes[0].m_sentinel_rank);
        EXPECT_EQ(8u, v_snap.m_nodes[0].m_total_logical_cores);
        EXPECT_EQ("beta", v_snap.m_nodes[1].m_hostname);
        EXPECT_EQ(1u, v_snap.m_nodes[1].m_sentinel_rank);
        EXPECT_EQ(4u, v_snap.m_nodes[1].m_total_logical_cores);
    }

    TEST(topology_builder_test, only_each_hosts_sentinel_sends_its_layout) {
        const std::string v_host = one_rank_snapshot(0).m_nodes.front().m_hostname;
        const std::vector<std::vector<std::vector<std::byte>>> v_replies{{identity_block(0, v_host), identity_block(1, v_host)}, {{}, {}}};
        scripted_transport v_sentinel(v_replies);
        scripted_transport v_other(v_replies);

        gempba::telemetry::build_topology_snapshot(v_sentinel, 0, 2);
        gempba::telemetry::build_topology_snapshot(v_other, 1, 2);

        ASSERT_EQ(2u, v_sentinel.m_sent.size());
        ASSERT_EQ(2u, v_other.m_sent.size());
        EXPECT_EQ(sizeof(gempba::telemetry::rank_identity), v_sentinel.m_sent[0].size());
        EXPECT_FALSE(v_sentinel.m_sent[1].empty());
        EXPECT_TRUE(v_other.m_sent[1].empty());
    }

    TEST(topology_builder_test, one_gathered_rank_describes_its_own_host) {
        scripted_transport v_transport({{identity_block(0, "alpha")}});
        const auto v_snap = gempba::telemetry::build_topology_snapshot(v_transport, 0, 2);

        ASSERT_EQ(1u, v_transport.m_sent.size());
        ASSERT_EQ(1u, v_snap.m_nodes.size());
        EXPECT_GE(v_snap.m_nodes.front().m_total_logical_cores, 1u);
    }

    TEST(topology_builder_test, hub_exposes_topology_after_runtime_ready) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        const auto& v_topo = v_hub.current_topology();
        ASSERT_EQ(1u, v_topo.m_nodes.size());
        EXPECT_EQ(1u, v_topo.m_nodes.front().m_ranks.size());
    }

    TEST(topology_builder_test, node_carries_logical_cores_and_memory) {
        const auto v_snap = one_rank_snapshot(0);
        const auto& v_node = v_snap.m_nodes.front();
        EXPECT_GE(v_node.m_total_logical_cores, 1u);
        EXPECT_GT(v_node.m_mem_total_bytes, 0u);
    }

    TEST(topology_builder_test, node_has_one_synthetic_socket_with_cpu_ids) {
        const auto v_snap = one_rank_snapshot(0);
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
        const auto v_snap = one_rank_snapshot(0);
        const auto& v_mask = v_snap.m_identities.front().m_allowed_cpu_mask;
        std::uint64_t v_any = 0;
        for (const std::uint64_t v_word: v_mask) {
            v_any |= v_word;
        }
        EXPECT_NE(0u, v_any);
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
