#include <csignal>
#include <cstring>
#include <gempba/core/stop.hpp>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/topology.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/json_serializer.hpp>
#include <optional>
#include <string>
#include <vector>

namespace {

    TEST(json_serializer_test, rank_frame_includes_core_fields) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_version = gempba::telemetry::TELEMETRY_SCHEMA_VERSION;
        v_f.m_rank = 3;
        v_f.m_seq_no = 7;
        v_f.m_tasks_started_total = 42;
        v_f.m_tasks_sent_total = 8;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"rank\":3"));
        EXPECT_NE(std::string::npos, v_out.find("\"seq_no\":7"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_started_total\":42"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_sent_total\":8"));
    }

    TEST(json_serializer_test, rank_frame_includes_the_transfer_bytes_and_times) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_bytes_sent_total = 31;
        v_f.m_bytes_recv_total = 32;
        v_f.m_send_microseconds_total = 33;
        v_f.m_recv_microseconds_total = 34;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"bytes_sent_total\":31"));
        EXPECT_NE(std::string::npos, v_out.find("\"bytes_recv_total\":32"));
        EXPECT_NE(std::string::npos, v_out.find("\"send_microseconds_total\":33"));
        EXPECT_NE(std::string::npos, v_out.find("\"recv_microseconds_total\":34"));
    }

    TEST(json_serializer_test, rank_frame_includes_the_spawn_and_score_counters) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_spawn_rejected_pool_full = 41;
        v_f.m_spawn_remote_rejected = 42;
        v_f.m_score_proposals_sent = 43;
        v_f.m_last_score_improvement_ms = 44;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"spawn_rejected_pool_full\":41"));
        EXPECT_NE(std::string::npos, v_out.find("\"spawn_remote_rejected\":42"));
        EXPECT_NE(std::string::npos, v_out.find("\"score_proposals_sent\":43"));
        EXPECT_NE(std::string::npos, v_out.find("\"last_score_improvement_ms\":44"));
    }

    TEST(json_serializer_test, rank_frame_includes_the_idle_split) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_idle_microseconds_per_thread = 900;
        v_f.m_idle_microseconds_starved = 600;
        v_f.m_idle_microseconds_imbalanced = 300;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_per_thread\":900"));
        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_starved\":600"));
        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_imbalanced\":300"));
    }

    TEST(json_serializer_test, rank_frame_includes_the_scheduler_times) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_sched_running_microseconds = 11;
        v_f.m_sched_waiting_microseconds = 12;
        v_f.m_first_task_ms = 13;
        v_f.m_last_task_ms = 14;
        v_f.m_assign_latency_microseconds_total = 15;
        v_f.m_assign_count = 16;
        v_f.m_assign_latency_microseconds_max = 17;
        v_f.m_available_microseconds_queue_empty = 18;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"sched_running_microseconds\":11"));
        EXPECT_NE(std::string::npos, v_out.find("\"sched_waiting_microseconds\":12"));
        EXPECT_NE(std::string::npos, v_out.find("\"first_task_ms\":13"));
        EXPECT_NE(std::string::npos, v_out.find("\"last_task_ms\":14"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_latency_microseconds_total\":15"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_count\":16"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_latency_microseconds_max\":17"));
        EXPECT_NE(std::string::npos, v_out.find("\"available_microseconds_queue_empty\":18"));
    }

    TEST(json_serializer_test, rank_frame_includes_the_coordinator_counters_and_telemetry_time) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_coordinator_state_msgs_total = 21;
        v_f.m_coordinator_score_proposals_total = 22;
        v_f.m_coordinator_score_broadcasts_total = 23;
        v_f.m_coordinator_full_microseconds = 24;
        v_f.m_telemetry_microseconds_total = 25;

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"coordinator_state_msgs_total\":21"));
        EXPECT_NE(std::string::npos, v_out.find("\"coordinator_score_proposals_total\":22"));
        EXPECT_NE(std::string::npos, v_out.find("\"coordinator_score_broadcasts_total\":23"));
        EXPECT_NE(std::string::npos, v_out.find("\"coordinator_full_microseconds\":24"));
        EXPECT_NE(std::string::npos, v_out.find("\"telemetry_microseconds_total\":25"));
    }

    TEST(json_serializer_test, rank_frame_emits_only_non_zero_edges) {
        gempba::telemetry::rank_frame v_f{};
        v_f.m_edges_out = {{2, 100, 5}, {7, 0, 0}};

        std::string v_out;
        gempba::telemetry::serialize_rank_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"to\":2"));
        EXPECT_EQ(std::string::npos, v_out.find("\"to\":7"));
    }

    TEST(json_serializer_test, node_frame_includes_hostname_and_memory) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_sentinel_rank = 1;
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

        EXPECT_NE(std::string::npos, v_out.find("\"sentinel_rank\":1"));
        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host-a\""));
        EXPECT_NE(std::string::npos, v_out.find("\"logical_cores\":16"));
        EXPECT_NE(std::string::npos, v_out.find("\"mem_total_bytes\":4096"));
        EXPECT_NE(std::string::npos, v_out.find("\"mem_avail_bytes\":1024"));
        EXPECT_NE(std::string::npos, v_out.find("\"cgroup_mem_used_bytes\":512"));
        EXPECT_NE(std::string::npos, v_out.find("\"cgroup_mem_limit_bytes\":8192"));
        EXPECT_NE(std::string::npos, v_out.find("\"cpu_pct\":12.500"));
    }

    TEST(json_serializer_test, node_frame_ends_with_its_sockets) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_socket_count = 1;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);

        EXPECT_EQ(std::string::npos, v_out.find("net_aggregate"));
        EXPECT_EQ(std::string::npos, v_out.find("disk_aggregate"));
        EXPECT_TRUE(v_out.ends_with("}]}")) << v_out;
    }

    TEST(json_serializer_test, topology_includes_node_and_ranks) {
        gempba::telemetry::topology_snapshot v_t;
        gempba::telemetry::topology_node v_node;
        v_node.m_hostname = "host-a";
        v_node.m_ranks = {0, 1, 2};
        v_node.m_sentinel_rank = 0;
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

        gempba::telemetry::rank_identity v_id{};
        v_id.m_rank = 0;
        std::strncpy(v_id.m_hostname, "host-a", sizeof(v_id.m_hostname));
        v_id.m_pid = 1234;
        v_id.m_primary_socket = 0;
        v_id.m_allowed_cpu_mask[0] = 0b1011ULL; // CPUs 0, 1, 3
        v_id.m_allowed_cpu_mask[1] = 1ULL << 36; // CPU 100 -- past the old 64-bit cap
        v_t.m_identities.push_back(v_id);

        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, v_t);

        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host-a\""));
        EXPECT_NE(std::string::npos, v_out.find("\"ranks\":[0,1,2]"));
        EXPECT_NE(std::string::npos, v_out.find("\"sentinel_rank\":0"));
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

    gempba::telemetry::topology_snapshot two_rank_topology() {
        gempba::telemetry::topology_snapshot v_topology;
        gempba::telemetry::rank_identity v_rank_0{};
        v_rank_0.m_rank = 0;
        gempba::telemetry::rank_identity v_rank_1{};
        v_rank_1.m_rank = 1;
        v_topology.m_identities = {v_rank_0, v_rank_1};
        return v_topology;
    }

    TEST(json_serializer_test, topology_run_carries_every_field_for_both_schedulers_and_load_balancers) {
        for (const std::string v_scheduler: {"centralized", "semi_centralized"}) {
            for (const std::string v_load_balancer: {"quasi_horizontal", "work_stealing"}) {
                gempba::telemetry::run_description v_run;
                v_run.m_mode = gempba::telemetry::runtime_mode::MP_MPI;
                v_run.m_scheduler = v_scheduler;
                v_run.m_load_balancer = v_load_balancer;
                v_run.m_world_size = 4;
                v_run.m_threads_per_process = 8;
                v_run.m_gempba_version = "4.3.0";
                v_run.m_annotations = {{"problem", "vertex_cover"}, {"seed", "42"}};

                std::string v_out;
                gempba::telemetry::serialize_topology(v_out, two_rank_topology(), &v_run);

                std::string v_expected = R"("run":{"scheduler":")";
                v_expected += v_scheduler;
                v_expected += R"(","load_balancer":")";
                v_expected += v_load_balancer;
                v_expected += R"(","runtime_mode":"multiprocessing","world_size":4,"threads_per_process":8,"gempba_version":"4.3.0",)";
                v_expected += R"("annotations":{"problem":"vertex_cover","seed":"42"},"markers":[]}})";
                EXPECT_TRUE(v_out.ends_with(v_expected)) << v_out;
            }
        }
    }

    TEST(json_serializer_test, topology_run_marks_the_ranks_the_scheduler_declares_coordinators) {
        gempba::telemetry::run_description v_run;
        v_run.m_mode = gempba::telemetry::runtime_mode::MP_MPI;
        v_run.m_roles = {gempba::rank_role::COORDINATOR, gempba::rank_role::WORKER};

        std::string v_mpi;
        gempba::telemetry::serialize_topology(v_mpi, two_rank_topology(), &v_run);
        EXPECT_NE(std::string::npos, v_mpi.find(R"({"rank":0,"hostname":"","pid":0,"primary_socket":0,"role":"coordinator")")) << v_mpi;
        EXPECT_NE(std::string::npos, v_mpi.find(R"({"rank":1,"hostname":"","pid":0,"primary_socket":0,"role":"worker")")) << v_mpi;

        v_run.m_mode = gempba::telemetry::runtime_mode::MT_ONLY;
        v_run.m_roles.clear();
        std::string v_mt;
        gempba::telemetry::serialize_topology(v_mt, two_rank_topology(), &v_run);
        EXPECT_EQ(std::string::npos, v_mt.find(R"("role":"coordinator")")) << v_mt;
        EXPECT_NE(std::string::npos, v_mt.find(R"("runtime_mode":"multithreading")")) << v_mt;
    }

    TEST(json_serializer_test, topology_run_marks_every_rank_a_worker_when_the_scheduler_declares_no_coordinator) {
        gempba::telemetry::run_description v_run;
        v_run.m_mode = gempba::telemetry::runtime_mode::MP_MPI;

        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, two_rank_topology(), &v_run);
        EXPECT_EQ(std::string::npos, v_out.find(R"("role":"coordinator")")) << v_out;
        EXPECT_NE(std::string::npos, v_out.find(R"({"rank":0,"hostname":"","pid":0,"primary_socket":0,"role":"worker")")) << v_out;
        EXPECT_NE(std::string::npos, v_out.find(R"({"rank":1,"hostname":"","pid":0,"primary_socket":0,"role":"worker")")) << v_out;
    }

    TEST(json_serializer_test, topology_run_writes_null_for_missing_components_and_escapes_annotations) {
        gempba::telemetry::run_description v_run;
        v_run.m_annotations = {{"label", "a \"quoted\" run"}};

        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, two_rank_topology(), &v_run);

        EXPECT_NE(std::string::npos, v_out.find(R"("run":{"scheduler":null,"load_balancer":null,)")) << v_out;
        EXPECT_NE(std::string::npos, v_out.find(R"("annotations":{"label":"a \"quoted\" run"},)")) << v_out;
    }

    TEST(json_serializer_test, topology_run_lists_the_markers_in_order_with_their_time_and_source) {
        gempba::telemetry::run_description v_run;
        v_run.m_markers = {{120, "setup done", gempba::telemetry::marker_source::CODE}, {4500, "phase \"2\"\n", gempba::telemetry::marker_source::DASHBOARD}};

        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, two_rank_topology(), &v_run);

        const std::string v_expected = R"("markers":[{"elapsed_ms":120,"label":"setup done","source":"code"},{"elapsed_ms":4500,"label":"phase \"2\"\n","source":"dashboard"}]}})";
        EXPECT_TRUE(v_out.ends_with(v_expected)) << v_out;
    }

    TEST(json_serializer_test, topology_without_a_run_has_no_run_or_roles) {
        std::string v_out;
        gempba::telemetry::serialize_topology(v_out, two_rank_topology());

        EXPECT_EQ(std::string::npos, v_out.find("\"run\""));
        EXPECT_EQ(std::string::npos, v_out.find("\"role\""));
        EXPECT_TRUE(v_out.ends_with("]}")) << v_out;
    }

    TEST(json_serializer_test, broadcast_envelope_carries_version_and_top_level_keys) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::rank_frame> v_rank_frames;
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_run_id = "123e4567-e89b-42d3-a456-426614174000";
        v_p.m_now_ms = 1700000000000ULL;
        v_p.m_elapsed_seconds = 12345ULL;
        v_p.m_topology = &v_topo;
        v_p.m_rank_frames = &v_rank_frames;
        v_p.m_nodes = &v_nodes;

        std::string v_out;
        gempba::telemetry::serialize_broadcast(v_out, v_p);

        EXPECT_TRUE(v_out.starts_with(R"({"version":2,"run_id":"123e4567-e89b-42d3-a456-426614174000","ts":1700000000000,)")) << v_out;
        EXPECT_TRUE(v_out.ends_with("}"));
        EXPECT_NE(std::string::npos, v_out.find("\"elapsed_seconds\":12345"));
        EXPECT_NE(std::string::npos, v_out.find("\"topology\":"));
        EXPECT_NE(std::string::npos, v_out.find("\"ranks\":[]"));
        EXPECT_NE(std::string::npos, v_out.find("\"nodes\":[]"));
    }

    TEST(json_serializer_test, broadcast_reports_whether_telemetry_is_paused_after_the_header) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::rank_frame> v_rank_frames(1);
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_elapsed_seconds = 12;
        v_p.m_topology = &v_topo;
        v_p.m_rank_frames = &v_rank_frames;
        v_p.m_nodes = &v_nodes;

        std::string v_running;
        gempba::telemetry::serialize_broadcast(v_running, v_p);
        v_p.m_paused = true;
        std::string v_paused;
        gempba::telemetry::serialize_broadcast(v_paused, v_p);

        EXPECT_NE(std::string::npos, v_running.find(R"("elapsed_seconds":12,"paused":false,"final":false,"topology":)")) << v_running;
        EXPECT_NE(std::string::npos, v_paused.find(R"("elapsed_seconds":12,"paused":true,"final":false,"topology":)")) << v_paused;
        EXPECT_NE(std::string::npos, v_paused.find(R"("ranks":[{"rank":0,)")) << v_paused;
    }

    TEST(json_serializer_test, paused_broadcast_is_the_header_alone) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::rank_frame> v_rank_frames(1);
        std::vector<gempba::telemetry::node_frame> v_nodes(1);

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_run_id = "123e4567-e89b-42d3-a456-426614174000";
        v_p.m_now_ms = 1700000000000ULL;
        v_p.m_elapsed_seconds = 12345ULL;
        v_p.m_topology = &v_topo;
        v_p.m_rank_frames = &v_rank_frames;
        v_p.m_nodes = &v_nodes;

        std::string v_out;
        gempba::telemetry::serialize_paused_broadcast(v_out, v_p);

        EXPECT_EQ(R"({"version":2,"run_id":"123e4567-e89b-42d3-a456-426614174000","ts":1700000000000,"elapsed_seconds":12345,"paused":true,"final":false})", v_out);
    }

    TEST(json_serializer_test, only_the_shutdown_line_is_marked_final) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::rank_frame> v_rank_frames;
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_elapsed_seconds = 12;
        v_p.m_topology = &v_topo;
        v_p.m_rank_frames = &v_rank_frames;
        v_p.m_nodes = &v_nodes;
        v_p.m_final = true;

        std::string v_final;
        gempba::telemetry::serialize_broadcast(v_final, v_p);
        v_p.m_paused = true;
        std::string v_paused_final;
        gempba::telemetry::serialize_broadcast(v_paused_final, v_p);

        EXPECT_NE(std::string::npos, v_final.find(R"("elapsed_seconds":12,"paused":false,"final":true,"topology":)")) << v_final;
        EXPECT_NE(std::string::npos, v_paused_final.find(R"("paused":true,"final":true,"topology":)")) << v_paused_final;
    }

    TEST(json_serializer_test, special_characters_in_hostname_are_escaped) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_sentinel_rank = 0;
        std::strncpy(v_n.m_hostname, "host\"a\\b", sizeof(v_n.m_hostname));
        v_n.m_socket_count = 1;
        v_n.m_sockets[0].m_socket_id = 0;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);
        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host\\\"a\\\\b\""));
    }

    std::string final_line_ending(const std::optional<gempba::run_outcome>& p_end, const bool p_final = true) {
        const gempba::telemetry::topology_snapshot v_topo;
        const std::vector<gempba::telemetry::rank_frame> v_rank_frames;
        const std::vector<gempba::telemetry::node_frame> v_nodes;
        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_topology = &v_topo;
        v_p.m_rank_frames = &v_rank_frames;
        v_p.m_nodes = &v_nodes;
        v_p.m_final = p_final;
        v_p.m_end = p_end;
        std::string v_line;
        gempba::telemetry::serialize_broadcast(v_line, v_p);
        return v_line;
    }

    TEST(json_serializer_test, the_final_line_says_a_completed_run_completed) {
        const std::string v_line = final_line_ending(gempba::run_outcome{});
        EXPECT_NE(std::string::npos, v_line.find(R"("final":true,"end":{"reason":"completed"},"topology":)")) << v_line;
    }

    TEST(json_serializer_test, the_final_line_names_the_signal_that_interrupted_the_run) {
        const std::string v_line = final_line_ending(gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGTERM});
        EXPECT_NE(std::string::npos, v_line.find(R"("final":true,"end":{"reason":"interrupted","trigger":"signal","signal":"SIGTERM"},"topology":)")) << v_line;
    }

    TEST(json_serializer_test, the_final_line_names_a_stop_by_code_or_control_without_a_signal) {
        const std::string v_code = final_line_ending(gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::CODE, 0});
        const std::string v_control = final_line_ending(gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::CONTROL, 0});
        EXPECT_NE(std::string::npos, v_code.find(R"("end":{"reason":"interrupted","trigger":"code"},)")) << v_code;
        EXPECT_NE(std::string::npos, v_control.find(R"("end":{"reason":"interrupted","trigger":"control"},)")) << v_control;
    }

    TEST(json_serializer_test, only_a_final_line_with_a_known_end_carries_one) {
        EXPECT_EQ(std::string::npos, final_line_ending(std::nullopt).find(R"("end":)"));
        EXPECT_EQ(std::string::npos, final_line_ending(gempba::run_outcome{}, false).find(R"("end":)"));
    }

} // namespace
