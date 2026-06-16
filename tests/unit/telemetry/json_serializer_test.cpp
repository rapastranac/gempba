#include <cstring>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/json_serializer.hpp>
#include <string>

namespace {

    TEST(json_serializer_test, worker_frame_includes_core_fields) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_version = gempba::telemetry::TELEMETRY_SCHEMA_VERSION;
        v_f.m_worker_id = 3;
        v_f.m_seq_no = 7;
        v_f.m_tasks_local_total = 42;
        v_f.m_tasks_sent_total = 8;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"worker_id\":3"));
        EXPECT_NE(std::string::npos, v_out.find("\"seq_no\":7"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_local_total\":42"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_sent_total\":8"));
    }

    TEST(json_serializer_test, worker_frame_emits_only_non_zero_edges) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_edges_out[2].m_bytes = 100;
        v_f.m_edges_out[2].m_count = 5;
        v_f.m_edges_out[7].m_bytes = 0;
        v_f.m_edges_out[7].m_count = 0;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"to\":2"));
        EXPECT_EQ(std::string::npos, v_out.find("\"to\":7"));
    }

    TEST(json_serializer_test, node_frame_includes_hostname_and_memory) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_sentinel_worker_id = 1;
        std::strncpy(v_n.m_hostname, "host-a", sizeof(v_n.m_hostname));
        v_n.m_socket_count = 1;
        v_n.m_logical_cores = 16;
        v_n.m_mem_total_bytes = 4096;
        v_n.m_mem_avail_bytes = 1024;
        v_n.m_cgroup_mem_used_bytes = 512;
        v_n.m_cgroup_mem_limit_bytes = 8192;
        v_n.m_sockets[0].m_socket_id = 0;
        v_n.m_sockets[0].m_cpu_pct = 12.5f;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);

        EXPECT_NE(std::string::npos, v_out.find("\"sentinel_worker_id\":1"));
        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host-a\""));
        EXPECT_NE(std::string::npos, v_out.find("\"logical_cores\":16"));
        EXPECT_NE(std::string::npos, v_out.find("\"mem_total_bytes\":4096"));
        EXPECT_NE(std::string::npos, v_out.find("\"mem_avail_bytes\":1024"));
        EXPECT_NE(std::string::npos, v_out.find("\"cgroup_mem_used_bytes\":512"));
        EXPECT_NE(std::string::npos, v_out.find("\"cgroup_mem_limit_bytes\":8192"));
        EXPECT_NE(std::string::npos, v_out.find("\"cpu_pct\":12.500"));
    }

    TEST(json_serializer_test, topology_includes_node_and_worker_ids) {
        gempba::telemetry::topology_snapshot v_t;
        gempba::telemetry::topology_node v_node;
        v_node.m_hostname = "host-a";
        v_node.m_worker_ids = {0, 1, 2};
        v_node.m_sentinel_worker_id = 0;
        v_node.m_total_physical_cores = 8;
        v_node.m_total_logical_cores = 16;
        v_node.m_mem_total_bytes = 17179869184ULL; // 16 GiB

        gempba::telemetry::topology_socket v_sock;
        v_sock.m_socket_id = 0;
        v_sock.m_name = "Test CPU 9000";
        v_sock.m_physical_cores = 8;
        v_sock.m_logical_cores = 16;
        v_sock.m_cpu_ids = {0, 1, 2, 3};
        v_node.m_sockets.push_back(std::move(v_sock));

        v_t.m_nodes.push_back(std::move(v_node));

        gempba::telemetry::worker_identity v_id{};
        v_id.m_worker_id = 0;
        std::strncpy(v_id.m_hostname, "host-a", sizeof(v_id.m_hostname));
        v_id.m_pid = 1234;
        v_id.m_primary_socket = 0;
        v_id.m_allowed_cpu_mask[0] = 0b1011ULL; // CPUs 0, 1, 3
        v_id.m_allowed_cpu_mask[1] = 1ULL << 36; // CPU 100 -- past the old 64-bit cap
        v_t.m_identities.push_back(v_id);

        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, v_t);

        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host-a\""));
        EXPECT_NE(std::string::npos, v_out.find("\"worker_ids\":[0,1,2]"));
        EXPECT_NE(std::string::npos, v_out.find("\"sentinel_worker_id\":0"));
        EXPECT_NE(std::string::npos, v_out.find("\"total_physical_cores\":8"));
        EXPECT_NE(std::string::npos, v_out.find("\"total_logical_cores\":16"));
        EXPECT_NE(std::string::npos, v_out.find("\"mem_total_bytes\":17179869184"));
        EXPECT_NE(std::string::npos, v_out.find("\"name\":\"Test CPU 9000\""));
        EXPECT_NE(std::string::npos, v_out.find("\"physical_cores\":8"));
        EXPECT_NE(std::string::npos, v_out.find("\"cpu_ids\":[0,1,2,3]"));
        EXPECT_NE(std::string::npos, v_out.find("\"pid\":1234"));
        // Word 0 bits 0,1,3 plus word 1 bit 36 → CPUs 0,1,3,100; the 100 proves
        // CPUs past the old 64-bit cap survive.
        EXPECT_NE(std::string::npos, v_out.find("\"allowed_cpu_ids\":[0,1,3,100]"));
    }

    TEST(json_serializer_test, broadcast_envelope_carries_version_and_top_level_keys) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::worker_frame> v_workers;
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_now_ms = 1700000000000ULL;
        v_p.m_elapsed_seconds = 12345ULL;
        v_p.m_topology = &v_topo;
        v_p.m_workers = &v_workers;
        v_p.m_nodes = &v_nodes;

        std::string v_out;
        gempba::telemetry::serialize_broadcast(v_out, v_p);

        EXPECT_TRUE(v_out.starts_with("{"));
        EXPECT_TRUE(v_out.ends_with("}"));
        EXPECT_NE(std::string::npos, v_out.find("\"version\":1"));
        EXPECT_NE(std::string::npos, v_out.find("\"ts\":1700000000000"));
        EXPECT_NE(std::string::npos, v_out.find("\"elapsed_seconds\":12345"));
        EXPECT_NE(std::string::npos, v_out.find("\"topology\":"));
        EXPECT_NE(std::string::npos, v_out.find("\"workers\":[]"));
        EXPECT_NE(std::string::npos, v_out.find("\"nodes\":[]"));
    }

    TEST(json_serializer_test, special_characters_in_hostname_are_escaped) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_sentinel_worker_id = 0;
        std::strncpy(v_n.m_hostname, "host\"a\\b", sizeof(v_n.m_hostname));
        v_n.m_socket_count = 1;
        v_n.m_sockets[0].m_socket_id = 0;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);
        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host\\\"a\\\\b\""));
    }

} // namespace
