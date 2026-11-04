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
        v_f.m_tasks_started_total = 42;
        v_f.m_tasks_sent_total = 8;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"worker_id\":3"));
        EXPECT_NE(std::string::npos, v_out.find("\"seq_no\":7"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_started_total\":42"));
        EXPECT_NE(std::string::npos, v_out.find("\"tasks_sent_total\":8"));
    }

    TEST(json_serializer_test, worker_frame_includes_the_transfer_bytes_and_times) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_bytes_sent_total = 31;
        v_f.m_bytes_recv_total = 32;
        v_f.m_send_microseconds_total = 33;
        v_f.m_recv_microseconds_total = 34;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"bytes_sent_total\":31"));
        EXPECT_NE(std::string::npos, v_out.find("\"bytes_recv_total\":32"));
        EXPECT_NE(std::string::npos, v_out.find("\"send_microseconds_total\":33"));
        EXPECT_NE(std::string::npos, v_out.find("\"recv_microseconds_total\":34"));
    }

    TEST(json_serializer_test, worker_frame_includes_the_spawn_and_score_counters) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_spawn_rejected_pool_full = 41;
        v_f.m_spawn_remote_rejected = 42;
        v_f.m_score_proposals_sent = 43;
        v_f.m_last_score_improvement_ms = 44;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"spawn_rejected_pool_full\":41"));
        EXPECT_NE(std::string::npos, v_out.find("\"spawn_remote_rejected\":42"));
        EXPECT_NE(std::string::npos, v_out.find("\"score_proposals_sent\":43"));
        EXPECT_NE(std::string::npos, v_out.find("\"last_score_improvement_ms\":44"));
    }

    TEST(json_serializer_test, worker_frame_includes_the_idle_split) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_idle_microseconds_per_worker = 900;
        v_f.m_idle_microseconds_starved = 600;
        v_f.m_idle_microseconds_imbalanced = 300;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_per_worker\":900"));
        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_starved\":600"));
        EXPECT_NE(std::string::npos, v_out.find("\"idle_microseconds_imbalanced\":300"));
    }

    TEST(json_serializer_test, worker_frame_includes_the_scheduler_times) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_sched_running_microseconds = 11;
        v_f.m_sched_waiting_microseconds = 12;
        v_f.m_first_task_ms = 13;
        v_f.m_last_task_ms = 14;
        v_f.m_assign_latency_microseconds_total = 15;
        v_f.m_assign_count = 16;
        v_f.m_assign_latency_microseconds_max = 17;
        v_f.m_available_microseconds_queue_empty = 18;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"sched_running_microseconds\":11"));
        EXPECT_NE(std::string::npos, v_out.find("\"sched_waiting_microseconds\":12"));
        EXPECT_NE(std::string::npos, v_out.find("\"first_task_ms\":13"));
        EXPECT_NE(std::string::npos, v_out.find("\"last_task_ms\":14"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_latency_microseconds_total\":15"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_count\":16"));
        EXPECT_NE(std::string::npos, v_out.find("\"assign_latency_microseconds_max\":17"));
        EXPECT_NE(std::string::npos, v_out.find("\"available_microseconds_queue_empty\":18"));
    }

    TEST(json_serializer_test, worker_frame_includes_the_center_counters_and_telemetry_time) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_center_state_msgs_total = 21;
        v_f.m_center_score_proposals_total = 22;
        v_f.m_center_score_broadcasts_total = 23;
        v_f.m_center_full_microseconds = 24;
        v_f.m_telemetry_microseconds_total = 25;

        std::string v_out;
        gempba::telemetry::serialize_worker_frame(v_out, v_f);

        EXPECT_NE(std::string::npos, v_out.find("\"center_state_msgs_total\":21"));
        EXPECT_NE(std::string::npos, v_out.find("\"center_score_proposals_total\":22"));
        EXPECT_NE(std::string::npos, v_out.find("\"center_score_broadcasts_total\":23"));
        EXPECT_NE(std::string::npos, v_out.find("\"center_full_microseconds\":24"));
        EXPECT_NE(std::string::npos, v_out.find("\"telemetry_microseconds_total\":25"));
    }

    TEST(json_serializer_test, worker_frame_emits_only_non_zero_edges) {
        gempba::telemetry::worker_frame v_f{};
        v_f.m_edges_out = {{2, 100, 5}, {7, 0, 0}};

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

    TEST(json_serializer_test, node_frame_ends_with_its_sockets) {
        gempba::telemetry::node_frame v_n{};
        v_n.m_socket_count = 1;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);

        EXPECT_EQ(std::string::npos, v_out.find("net_aggregate"));
        EXPECT_EQ(std::string::npos, v_out.find("disk_aggregate"));
        EXPECT_TRUE(v_out.ends_with("}]}")) << v_out;
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

    gempba::telemetry::topology_snapshot two_rank_topology() {
        gempba::telemetry::topology_snapshot v_topology;
        gempba::telemetry::worker_identity v_center{};
        v_center.m_worker_id = 0;
        gempba::telemetry::worker_identity v_worker{};
        v_worker.m_worker_id = 1;
        v_topology.m_identities = {v_center, v_worker};
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

    TEST(json_serializer_test, topology_run_marks_rank_zero_as_the_center_only_under_mpi) {
        gempba::telemetry::run_description v_run;
        v_run.m_mode = gempba::telemetry::runtime_mode::MP_MPI;

        std::string v_mpi;
        gempba::telemetry::serialize_topology(v_mpi, two_rank_topology(), &v_run);
        EXPECT_NE(std::string::npos, v_mpi.find(R"({"worker_id":0,"hostname":"","pid":0,"primary_socket":0,"role":"center")")) << v_mpi;
        EXPECT_NE(std::string::npos, v_mpi.find(R"({"worker_id":1,"hostname":"","pid":0,"primary_socket":0,"role":"worker")")) << v_mpi;

        v_run.m_mode = gempba::telemetry::runtime_mode::MT_ONLY;
        std::string v_mt;
        gempba::telemetry::serialize_topology(v_mt, two_rank_topology(), &v_run);
        EXPECT_EQ(std::string::npos, v_mt.find(R"("role":"center")")) << v_mt;
        EXPECT_NE(std::string::npos, v_mt.find(R"("runtime_mode":"multithreading")")) << v_mt;
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
        std::vector<gempba::telemetry::worker_frame> v_workers;
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_run_id = "123e4567-e89b-42d3-a456-426614174000";
        v_p.m_now_ms = 1700000000000ULL;
        v_p.m_elapsed_seconds = 12345ULL;
        v_p.m_topology = &v_topo;
        v_p.m_workers = &v_workers;
        v_p.m_nodes = &v_nodes;

        std::string v_out;
        gempba::telemetry::serialize_broadcast(v_out, v_p);

        EXPECT_TRUE(v_out.starts_with(R"({"version":2,"run_id":"123e4567-e89b-42d3-a456-426614174000","ts":1700000000000,)")) << v_out;
        EXPECT_TRUE(v_out.ends_with("}"));
        EXPECT_NE(std::string::npos, v_out.find("\"elapsed_seconds\":12345"));
        EXPECT_NE(std::string::npos, v_out.find("\"topology\":"));
        EXPECT_NE(std::string::npos, v_out.find("\"workers\":[]"));
        EXPECT_NE(std::string::npos, v_out.find("\"nodes\":[]"));
    }

    TEST(json_serializer_test, broadcast_reports_whether_telemetry_is_paused_after_the_header) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::worker_frame> v_workers(1);
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_elapsed_seconds = 12;
        v_p.m_topology = &v_topo;
        v_p.m_workers = &v_workers;
        v_p.m_nodes = &v_nodes;

        std::string v_running;
        gempba::telemetry::serialize_broadcast(v_running, v_p);
        v_p.m_paused = true;
        std::string v_paused;
        gempba::telemetry::serialize_broadcast(v_paused, v_p);

        EXPECT_NE(std::string::npos, v_running.find(R"("elapsed_seconds":12,"paused":false,"final":false,"topology":)")) << v_running;
        EXPECT_NE(std::string::npos, v_paused.find(R"("elapsed_seconds":12,"paused":true,"final":false,"topology":)")) << v_paused;
        EXPECT_NE(std::string::npos, v_paused.find(R"("workers":[{"worker_id":0,)")) << v_paused;
    }

    TEST(json_serializer_test, paused_broadcast_is_the_header_alone) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::worker_frame> v_workers(1);
        std::vector<gempba::telemetry::node_frame> v_nodes(1);

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_run_id = "123e4567-e89b-42d3-a456-426614174000";
        v_p.m_now_ms = 1700000000000ULL;
        v_p.m_elapsed_seconds = 12345ULL;
        v_p.m_topology = &v_topo;
        v_p.m_workers = &v_workers;
        v_p.m_nodes = &v_nodes;

        std::string v_out;
        gempba::telemetry::serialize_paused_broadcast(v_out, v_p);

        EXPECT_EQ(R"({"version":2,"run_id":"123e4567-e89b-42d3-a456-426614174000","ts":1700000000000,"elapsed_seconds":12345,"paused":true,"final":false})", v_out);
    }

    TEST(json_serializer_test, only_the_shutdown_line_is_marked_final) {
        gempba::telemetry::topology_snapshot v_topo;
        std::vector<gempba::telemetry::worker_frame> v_workers;
        std::vector<gempba::telemetry::node_frame> v_nodes;

        gempba::telemetry::broadcast_payload v_p{};
        v_p.m_elapsed_seconds = 12;
        v_p.m_topology = &v_topo;
        v_p.m_workers = &v_workers;
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
        v_n.m_sentinel_worker_id = 0;
        std::strncpy(v_n.m_hostname, "host\"a\\b", sizeof(v_n.m_hostname));
        v_n.m_socket_count = 1;
        v_n.m_sockets[0].m_socket_id = 0;

        std::string v_out;
        gempba::telemetry::serialize_node_frame(v_out, v_n);
        EXPECT_NE(std::string::npos, v_out.find("\"hostname\":\"host\\\"a\\\\b\""));
    }

} // namespace
