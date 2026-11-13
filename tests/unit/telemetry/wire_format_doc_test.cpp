#include <csignal>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <impl/telemetry/json_serializer.hpp>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

    std::string read_wire_format_doc() {
        const std::filesystem::path v_repo_root = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
        std::ifstream v_file(v_repo_root / "docs" / "telemetry" / "wire-format.md");
        std::ostringstream v_text;
        v_text << v_file.rdbuf();
        return v_text.str();
    }

    std::set<std::string> keys_in(const std::string& p_json) {
        const std::regex v_key(R"re("([a-z_]+)":)re");
        std::set<std::string> v_keys;
        for (auto v_it = std::sregex_iterator(p_json.begin(), p_json.end(), v_key); v_it != std::sregex_iterator(); ++v_it) {
            v_keys.insert((*v_it)[1].str());
        }
        return v_keys;
    }

    TEST(wire_format_doc_test, documents_every_key_a_broadcast_sends) {
        gempba::telemetry::topology_snapshot v_topology;
        gempba::telemetry::topology_node v_node;
        v_node.m_ranks = {0, 1};
        v_node.m_sockets.push_back(gempba::telemetry::topology_socket{0, "cpu", 2, 4, {0, 1, 2, 3}});
        v_topology.m_nodes.push_back(v_node);
        v_topology.m_identities.resize(2);
        v_topology.m_identities[1].m_rank = 1;

        gempba::telemetry::run_description v_run;
        v_run.m_mode = gempba::telemetry::runtime_mode::MP_MPI;
        v_run.m_markers = {{120, "phase 2", gempba::telemetry::marker_source::DASHBOARD}};

        std::vector<gempba::telemetry::rank_frame> v_rank_frames(1);
        v_rank_frames[0].m_edges_out.push_back(gempba::telemetry::edge_counter{1, 64, 1});
        std::vector<gempba::telemetry::node_frame> v_nodes(1);
        v_nodes[0].m_socket_count = 1;

        gempba::telemetry::broadcast_payload v_payload{};
        v_payload.m_run_id = "123e4567-e89b-42d3-a456-426614174000";
        v_payload.m_topology = &v_topology;
        v_payload.m_run = &v_run;
        v_payload.m_rank_frames = &v_rank_frames;
        v_payload.m_nodes = &v_nodes;
        v_payload.m_final = true;
        v_payload.m_end = gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGTERM};

        std::string v_lines;
        gempba::telemetry::serialize_broadcast(v_lines, v_payload);
        gempba::telemetry::serialize_paused_broadcast(v_lines, v_payload);
        const std::set<std::string> v_keys = keys_in(v_lines);
        for (const char* v_nested: {"paused", "role", "cpu_ids", "elapsed_ms", "to", "cpu_pct", "signal"}) {
            ASSERT_TRUE(v_keys.contains(v_nested)) << "the sample broadcast no longer reaches `" << v_nested << "`";
        }

        const std::string v_doc = read_wire_format_doc();
        ASSERT_FALSE(v_doc.empty()) << "docs/telemetry/wire-format.md was not found";
        for (const std::string& v_key: v_keys) {
            EXPECT_NE(std::string::npos, v_doc.find('`' + v_key + '`')) << "docs/telemetry/wire-format.md does not mention `" << v_key << "`";
        }
    }

} // namespace
