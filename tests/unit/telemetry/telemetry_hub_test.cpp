#include <atomic>
#include <chrono>
#include <gempba/config.h>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

    class telemetry_hub_test : public ::testing::Test {
    protected:
        void TearDown() override {
            gempba::telemetry::uninstall();
            gempba::telemetry::enable();
        }
    };

    TEST_F(telemetry_hub_test, get_returns_nullptr_when_no_hub_installed) { EXPECT_EQ(nullptr, gempba::telemetry::get()); }

    TEST_F(telemetry_hub_test, install_then_get_returns_same_instance) {
        auto v_hub = std::make_unique<gempba::telemetry::telemetry_hub>();
        const auto* v_raw = v_hub.get();
        gempba::telemetry::install(std::move(v_hub));
        EXPECT_EQ(v_raw, gempba::telemetry::get());
    }

    TEST_F(telemetry_hub_test, uninstall_clears_get) {
        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        ASSERT_NE(nullptr, gempba::telemetry::get());
        gempba::telemetry::uninstall();
        EXPECT_EQ(nullptr, gempba::telemetry::get());
    }

    TEST_F(telemetry_hub_test, disable_tears_down_installed_hub_and_blocks_install) {
        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        ASSERT_NE(nullptr, gempba::telemetry::get());

        gempba::telemetry::disable();
        EXPECT_EQ(nullptr, gempba::telemetry::get());
        EXPECT_FALSE(gempba::telemetry::is_enabled());

        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        EXPECT_EQ(nullptr, gempba::telemetry::get());
    }

    TEST_F(telemetry_hub_test, disable_before_install_blocks_install) {
        gempba::telemetry::disable();
        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        EXPECT_EQ(nullptr, gempba::telemetry::get());
    }

    TEST_F(telemetry_hub_test, enable_after_disable_allows_install_again) {
        gempba::telemetry::disable();
        gempba::telemetry::enable();
        EXPECT_TRUE(gempba::telemetry::is_enabled());

        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        EXPECT_NE(nullptr, gempba::telemetry::get());
    }

    TEST_F(telemetry_hub_test, disable_and_enable_are_idempotent) {
        EXPECT_NO_THROW(gempba::telemetry::disable());
        EXPECT_NO_THROW(gempba::telemetry::disable());
        EXPECT_NO_THROW(gempba::telemetry::enable());
        EXPECT_NO_THROW(gempba::telemetry::enable());
        EXPECT_TRUE(gempba::telemetry::is_enabled());
    }

    TEST_F(telemetry_hub_test, on_runtime_ready_does_not_throw_in_either_mode) {
        gempba::telemetry::telemetry_hub v_hub;
        EXPECT_NO_THROW(v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false));
        gempba::telemetry::telemetry_hub v_other;
        EXPECT_NO_THROW(v_other.on_runtime_ready(gempba::telemetry::runtime_mode::MP_MPI, 3, 8, /*p_start_pump_thread=*/false));
    }

    TEST_F(telemetry_hub_test, shutdown_is_idempotent) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        EXPECT_NO_THROW(v_hub.shutdown());
        EXPECT_NO_THROW(v_hub.shutdown());
    }

    TEST_F(telemetry_hub_test, center_shutdown_waits_at_most_a_second_for_missing_final_frames) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MP_MPI, 0, 3, /*p_start_pump_thread=*/false);

        const auto v_start = std::chrono::steady_clock::now();
        v_hub.shutdown();
        const auto v_elapsed = std::chrono::steady_clock::now() - v_start;

        EXPECT_GE(v_elapsed, std::chrono::milliseconds(900));
        EXPECT_LT(v_elapsed, std::chrono::seconds(3));
    }

    TEST_F(telemetry_hub_test, single_process_shutdown_does_not_wait_for_final_frames) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        const auto v_start = std::chrono::steady_clock::now();
        v_hub.shutdown();

        EXPECT_LT(std::chrono::steady_clock::now() - v_start, std::chrono::milliseconds(500));
    }

    TEST_F(telemetry_hub_test, program_elapsed_seconds_is_zero_before_runtime_ready) {
        const gempba::telemetry::telemetry_hub v_hub;
        EXPECT_EQ(0u, v_hub.program_elapsed_seconds());
    }

    TEST_F(telemetry_hub_test, program_elapsed_seconds_grows_after_runtime_ready) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        const auto v_first = v_hub.program_elapsed_seconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        const auto v_second = v_hub.program_elapsed_seconds();
        EXPECT_GT(v_second, v_first);
    }

    TEST_F(telemetry_hub_test, snapshot_reports_zero_counters_on_a_fresh_hub) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 7, 1, /*p_start_pump_thread=*/false);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);

        EXPECT_EQ(gempba::telemetry::TELEMETRY_SCHEMA_VERSION, v_frame.m_version);
        EXPECT_EQ(7u, v_frame.m_worker_id);
        EXPECT_EQ(0u, v_frame.m_tasks_started_total);
        EXPECT_EQ(0u, v_frame.m_tasks_sent_total);
        EXPECT_EQ(0u, v_frame.m_tasks_recv_total);
        EXPECT_EQ(0u, v_frame.m_bytes_sent_total);
        EXPECT_EQ(0u, v_frame.m_bytes_recv_total);
        EXPECT_EQ(0u, v_frame.m_send_microseconds_total);
        EXPECT_EQ(0u, v_frame.m_recv_microseconds_total);
        EXPECT_EQ(0u, v_frame.m_tasks_running);
        EXPECT_EQ(0u, v_frame.m_scheduler_pending_count);
        EXPECT_EQ(0u, v_frame.m_idle_microseconds_per_worker);
        EXPECT_EQ(0u, v_frame.m_idle_microseconds_starved);
        EXPECT_EQ(0u, v_frame.m_idle_microseconds_imbalanced);
        EXPECT_EQ(0u, v_frame.m_sched_running_microseconds);
        EXPECT_EQ(0u, v_frame.m_sched_waiting_microseconds);
        EXPECT_EQ(0u, v_frame.m_assign_count);
        EXPECT_EQ(0u, v_frame.m_available_microseconds_queue_empty);
        EXPECT_EQ(0u, v_frame.m_center_state_msgs_total);
        EXPECT_EQ(0u, v_frame.m_center_score_proposals_total);
        EXPECT_EQ(0u, v_frame.m_center_score_broadcasts_total);
        EXPECT_EQ(0u, v_frame.m_center_full_microseconds);
        EXPECT_EQ(0u, v_frame.m_telemetry_microseconds_total);
        EXPECT_EQ(0u, v_frame.m_spawn_rejected_pool_full);
        EXPECT_EQ(0u, v_frame.m_spawn_remote_rejected);
        EXPECT_EQ(0u, v_frame.m_score_proposals_sent);
        EXPECT_EQ(0u, v_frame.m_last_score_improvement_ms);
        EXPECT_GT(v_frame.m_worker_local_ms, 0u);
    }

    TEST_F(telemetry_hub_test, center_state_msg_counter_accumulates) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_center_state_msg();
        v_hub.record_center_state_msg();
        v_hub.record_center_state_msg();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(3u, v_frame.m_center_state_msgs_total);
    }

    TEST_F(telemetry_hub_test, center_score_proposal_counter_accumulates) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_center_score_proposal();
        v_hub.record_center_score_proposal();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_center_score_proposals_total);
    }

    TEST_F(telemetry_hub_test, center_score_broadcast_counter_accumulates) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_center_score_broadcast();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(1u, v_frame.m_center_score_broadcasts_total);
    }

    TEST_F(telemetry_hub_test, score_proposal_counter_accumulates) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 2, /*p_start_pump_thread=*/false);

        v_hub.record_score_proposal_sent();
        v_hub.record_score_proposal_sent();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_score_proposals_sent);
    }

    TEST_F(telemetry_hub_test, last_score_improvement_fits_in_the_run) {
        using clock = std::chrono::steady_clock;
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        const auto v_before = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
        v_hub.record_run_start();
        v_hub.record_global_score_improved();
        const auto v_after = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_LE(v_frame.m_last_score_improvement_ms, static_cast<std::uint64_t>(v_after - v_before));
    }

    TEST_F(telemetry_hub_test, center_full_time_fits_in_the_full_period_and_stops_when_free) {
        using clock = std::chrono::steady_clock;
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.record_run_start();

        const auto v_before = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
        v_hub.record_center_full();
        v_hub.record_center_free();
        const auto v_after = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();

        gempba::telemetry::worker_frame v_first{};
        v_hub.snapshot_worker_frame(v_first);
        gempba::telemetry::worker_frame v_second{};
        v_hub.snapshot_worker_frame(v_second);

        EXPECT_LE(v_first.m_center_full_microseconds, static_cast<std::uint64_t>(v_after - v_before));
        EXPECT_EQ(v_first.m_center_full_microseconds, v_second.m_center_full_microseconds);
    }

    TEST_F(telemetry_hub_test, tick_if_due_accumulates_its_own_time) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);

        for (int v_i = 0; v_i < 1000; ++v_i) {
            v_hub.tick_if_due();
        }

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_GT(v_frame.m_telemetry_microseconds_total, 0u);
    }

    TEST_F(telemetry_hub_test, sentinel_publishes_its_node_frame_at_the_worker_interval) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);
        v_hub.tick_if_due();
        const auto v_first = v_hub.latest_node_frame(0);
        ASSERT_TRUE(v_first.has_value());

        v_hub.set_worker_interval_ms(600'000);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (int v_i = 0; v_i < 100; ++v_i) {
            v_hub.tick_if_due();
        }
        EXPECT_EQ(v_first->m_sentinel_local_ms, v_hub.latest_node_frame(0)->m_sentinel_local_ms);

        v_hub.set_worker_interval_ms(0);
        v_hub.tick_if_due();
        EXPECT_GT(v_hub.latest_node_frame(0)->m_sentinel_local_ms, v_first->m_sentinel_local_ms);
    }

    TEST_F(telemetry_hub_test, tick_if_due_with_nothing_due_adds_no_time) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 2, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);
        v_hub.tick_if_due();
        v_hub.set_worker_interval_ms(600'000);

        gempba::telemetry::worker_frame v_before{};
        v_hub.snapshot_worker_frame(v_before);
        for (int v_i = 0; v_i < 1000; ++v_i) {
            v_hub.tick_if_due();
        }
        gempba::telemetry::worker_frame v_after{};
        v_hub.snapshot_worker_frame(v_after);

        EXPECT_EQ(v_before.m_telemetry_microseconds_total, v_after.m_telemetry_microseconds_total);
    }

    TEST_F(telemetry_hub_test, a_paused_hub_neither_publishes_nor_ingests_frames) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);
        v_hub.tick_if_due();
        const auto v_worker = v_hub.latest_worker_frame(0);
        const auto v_node = v_hub.latest_node_frame(0);
        ASSERT_TRUE(v_worker.has_value());
        ASSERT_TRUE(v_node.has_value());

        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);
        EXPECT_TRUE(v_hub.paused());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        for (int v_i = 0; v_i < 100; ++v_i) {
            v_hub.tick_if_due();
        }

        EXPECT_EQ(v_worker->m_seq_no, v_hub.latest_worker_frame(0)->m_seq_no);
        EXPECT_EQ(v_node->m_sentinel_local_ms, v_hub.latest_node_frame(0)->m_sentinel_local_ms);
    }

    TEST_F(telemetry_hub_test, paused_ticks_add_no_time) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);

        gempba::telemetry::worker_frame v_before{};
        v_hub.snapshot_worker_frame(v_before);
        for (int v_i = 0; v_i < 1000; ++v_i) {
            v_hub.tick_if_due();
        }
        gempba::telemetry::worker_frame v_after{};
        v_hub.snapshot_worker_frame(v_after);

        EXPECT_EQ(v_before.m_telemetry_microseconds_total, v_after.m_telemetry_microseconds_total);
    }

    TEST_F(telemetry_hub_test, resume_publishes_at_the_earlier_interval_with_what_was_counted_while_paused) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);
        v_hub.tick_if_due();
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::SET_WORKER_INTERVAL_MS, 50);

        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);
        for (int v_i = 0; v_i < 5; ++v_i) {
            v_hub.record_task_local();
            v_hub.record_send(0, 64);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        for (int v_i = 0; v_i < 10; ++v_i) {
            v_hub.tick_if_due();
        }
        EXPECT_EQ(0u, v_hub.latest_worker_frame(0)->m_tasks_started_total);

        v_hub.apply_control_from_client(gempba::telemetry::control_kind::RESUME, 0);
        EXPECT_FALSE(v_hub.paused());
        EXPECT_EQ(50u, v_hub.worker_interval_ms());
        v_hub.tick_if_due();

        const auto v_frame = v_hub.latest_worker_frame(0);
        EXPECT_EQ(5u, v_frame->m_tasks_started_total);
        EXPECT_EQ(5u, v_frame->m_tasks_sent_total);
        EXPECT_EQ(320u, v_frame->m_bytes_sent_total);
    }

    TEST_F(telemetry_hub_test, worker_scheduler_times_fit_in_the_run_and_stop_at_its_end) {
        using clock = std::chrono::steady_clock;
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 2, /*p_start_pump_thread=*/false);

        const auto v_before = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();
        v_hub.record_run_start();
        v_hub.record_worker_waiting();
        v_hub.record_worker_running();
        v_hub.record_worker_waiting();
        v_hub.record_run_end();
        const auto v_after = std::chrono::duration_cast<std::chrono::microseconds>(clock::now().time_since_epoch()).count();

        gempba::telemetry::worker_frame v_first{};
        v_hub.snapshot_worker_frame(v_first);
        gempba::telemetry::worker_frame v_second{};
        v_hub.snapshot_worker_frame(v_second);

        EXPECT_LE(v_first.m_sched_running_microseconds + v_first.m_sched_waiting_microseconds, static_cast<std::uint64_t>(v_after - v_before));
        EXPECT_EQ(v_first.m_sched_running_microseconds, v_second.m_sched_running_microseconds);
        EXPECT_EQ(v_first.m_sched_waiting_microseconds, v_second.m_sched_waiting_microseconds);
    }

    TEST_F(telemetry_hub_test, center_assignment_reaches_the_frame) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_assignment(3);
        v_hub.record_assignment_accepted(3);
        v_hub.record_assignment_accepted(3);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(1u, v_frame.m_assign_count);
        EXPECT_EQ(v_frame.m_assign_latency_microseconds_total, v_frame.m_assign_latency_microseconds_max);
    }

    TEST_F(telemetry_hub_test, center_generates_a_run_id_per_run_and_workers_have_none) {
        const std::regex v_uuid_v4{"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"};

        gempba::telemetry::telemetry_hub v_first_run;
        v_first_run.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        gempba::telemetry::telemetry_hub v_second_run;
        v_second_run.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        gempba::telemetry::telemetry_hub v_worker;
        v_worker.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 2, /*p_start_pump_thread=*/false);

        EXPECT_TRUE(std::regex_match(v_first_run.run_id(), v_uuid_v4)) << v_first_run.run_id();
        EXPECT_NE(v_first_run.run_id(), v_second_run.run_id());
        EXPECT_TRUE(v_worker.run_id().empty());
    }

    TEST_F(telemetry_hub_test, run_annotations_keep_their_order_and_a_repeated_key_replaces_its_value) {
        gempba::telemetry::telemetry_hub v_hub;

        EXPECT_TRUE(v_hub.annotate_run("problem", "vertex_cover"));
        EXPECT_TRUE(v_hub.annotate_run("seed", "42"));
        EXPECT_TRUE(v_hub.annotate_run("problem", "max_clique"));

        const std::vector<std::pair<std::string, std::string>> v_expected{{"problem", "max_clique"}, {"seed", "42"}};
        EXPECT_EQ(v_expected, v_hub.run_annotations());
    }

    TEST_F(telemetry_hub_test, run_annotations_beyond_the_cap_are_dropped) {
        gempba::telemetry::telemetry_hub v_hub;
        for (std::size_t v_i = 0; v_i < gempba::telemetry::MAX_RUN_ANNOTATIONS; ++v_i) {
            EXPECT_TRUE(v_hub.annotate_run("key" + std::to_string(v_i), "v"));
        }

        EXPECT_FALSE(v_hub.annotate_run("one_too_many", "v"));
        EXPECT_TRUE(v_hub.annotate_run("key0", "replaced"));

        const auto v_annotations = v_hub.run_annotations();
        ASSERT_EQ(gempba::telemetry::MAX_RUN_ANNOTATIONS, v_annotations.size());
        EXPECT_EQ("replaced", v_annotations.front().second);
    }

    TEST_F(telemetry_hub_test, run_annotations_over_a_length_limit_are_dropped_not_truncated) {
        gempba::telemetry::telemetry_hub v_hub;
        const std::string v_long_key(gempba::telemetry::MAX_RUN_ANNOTATION_KEY_BYTES + 1, 'k');
        const std::string v_long_value(gempba::telemetry::MAX_RUN_ANNOTATION_VALUE_BYTES + 1, 'v');

        EXPECT_FALSE(v_hub.annotate_run(v_long_key, "v"));
        EXPECT_FALSE(v_hub.annotate_run("key", v_long_value));
        EXPECT_FALSE(v_hub.annotate_run("", "v"));
        EXPECT_TRUE(v_hub.annotate_run(std::string(gempba::telemetry::MAX_RUN_ANNOTATION_KEY_BYTES, 'k'), std::string(gempba::telemetry::MAX_RUN_ANNOTATION_VALUE_BYTES, 'v')));

        EXPECT_EQ(1u, v_hub.run_annotations().size());
    }

    TEST_F(telemetry_hub_test, annotate_run_reaches_the_installed_hub_and_is_dropped_without_one) {
        EXPECT_FALSE(gempba::telemetry::annotate_run("label", "before_install"));

        gempba::telemetry::install(std::make_unique<gempba::telemetry::telemetry_hub>());
        EXPECT_TRUE(gempba::telemetry::annotate_run("label", "baseline"));

        const std::vector<std::pair<std::string, std::string>> v_expected{{"label", "baseline"}};
        EXPECT_EQ(v_expected, gempba::telemetry::get()->run_annotations());
    }

    TEST_F(telemetry_hub_test, describe_run_reports_the_mode_size_version_and_annotations) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MP_MPI, 3, 8, /*p_start_pump_thread=*/false);
        v_hub.annotate_run("problem", "vertex_cover");

        const gempba::telemetry::run_description v_run = v_hub.describe_run();
        EXPECT_EQ(gempba::telemetry::runtime_mode::MP_MPI, v_run.m_mode);
        EXPECT_EQ(8u, v_run.m_world_size);
        EXPECT_EQ(GEMPBA_VERSION, v_run.m_gempba_version);
        EXPECT_TRUE(v_run.m_scheduler.empty());
        EXPECT_TRUE(v_run.m_load_balancer.empty());
        EXPECT_EQ(0u, v_run.m_threads_per_process);
        const std::vector<std::pair<std::string, std::string>> v_expected{{"problem", "vertex_cover"}};
        EXPECT_EQ(v_expected, v_run.m_annotations);
    }

    TEST_F(telemetry_hub_test, describe_run_names_both_load_balancers_and_the_scheduler) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.describe_scheduler("semi_centralized");

        v_hub.describe_load_balancer(gempba::QUASI_HORIZONTAL);
        EXPECT_EQ("quasi_horizontal", v_hub.describe_run().m_load_balancer);
        v_hub.describe_load_balancer(gempba::WORK_STEALING);
        EXPECT_EQ("work_stealing", v_hub.describe_run().m_load_balancer);
        EXPECT_EQ("semi_centralized", v_hub.describe_run().m_scheduler);
    }

    void expect_edge(const gempba::telemetry::edge_counter& p_edge, const std::uint32_t p_dst, const std::uint64_t p_bytes, const std::uint64_t p_count) {
        EXPECT_EQ(p_dst, p_edge.m_dst_worker_id);
        EXPECT_EQ(p_bytes, p_edge.m_bytes);
        EXPECT_EQ(p_count, p_edge.m_count);
    }

    TEST_F(telemetry_hub_test, record_send_lists_only_the_destinations_sent_to_in_ascending_order) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 8, /*p_start_pump_thread=*/false);

        v_hub.record_send(5, 999);
        v_hub.record_send(2, 100);
        v_hub.record_send(2, 50);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);

        EXPECT_EQ(3u, v_frame.m_tasks_sent_total);
        ASSERT_EQ(2u, v_frame.m_edges_out.size());
        expect_edge(v_frame.m_edges_out[0], 2, 150, 2);
        expect_edge(v_frame.m_edges_out[1], 5, 999, 1);
    }

    TEST_F(telemetry_hub_test, record_send_records_edges_past_rank_1024) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 1100, /*p_start_pump_thread=*/false);

        v_hub.record_send(1050, 10);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        ASSERT_EQ(1u, v_frame.m_edges_out.size());
        expect_edge(v_frame.m_edges_out[0], 1050, 10, 1);
    }

    TEST_F(telemetry_hub_test, record_send_to_out_of_range_id_still_bumps_total) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 8, /*p_start_pump_thread=*/false);

        v_hub.record_send(20, 64);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(1u, v_frame.m_tasks_sent_total);
        EXPECT_EQ(64u, v_frame.m_bytes_sent_total);
        EXPECT_TRUE(v_frame.m_edges_out.empty());
    }

    TEST_F(telemetry_hub_test, record_send_accumulates_bytes_matching_the_edges_and_time) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 8, /*p_start_pump_thread=*/false);

        v_hub.record_send(2, 100, std::chrono::microseconds(3));
        v_hub.record_send(2, 50, std::chrono::microseconds(4));
        v_hub.record_send(5, 999, std::chrono::microseconds(5));

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        std::uint64_t v_edge_bytes = 0;
        for (const auto& v_edge: v_frame.m_edges_out) {
            v_edge_bytes += v_edge.m_bytes;
        }
        EXPECT_EQ(1149u, v_frame.m_bytes_sent_total);
        EXPECT_EQ(v_edge_bytes, v_frame.m_bytes_sent_total);
        EXPECT_EQ(12u, v_frame.m_send_microseconds_total);
    }

    TEST_F(telemetry_hub_test, record_recv_accumulates_count_bytes_and_time) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_recv(3, 1024, std::chrono::microseconds(7));
        v_hub.record_recv(3, 2048, std::chrono::microseconds(8));

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_tasks_recv_total);
        EXPECT_EQ(3072u, v_frame.m_bytes_recv_total);
        EXPECT_EQ(15u, v_frame.m_recv_microseconds_total);
    }

    TEST_F(telemetry_hub_test, sub_microsecond_transfer_times_add_up) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        for (int v_i = 0; v_i < 5; ++v_i) {
            v_hub.record_send(1, 8, std::chrono::nanoseconds(400));
            v_hub.record_recv(1, 8, std::chrono::nanoseconds(600));
        }

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_send_microseconds_total);
        EXPECT_EQ(3u, v_frame.m_recv_microseconds_total);
    }

    TEST_F(telemetry_hub_test, snapshot_leaves_runtime_fields_zero_without_load_balancer_or_scheduler) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(0u, v_frame.m_tasks_running);
        EXPECT_EQ(0u, v_frame.m_scheduler_pending_count);
    }

    TEST_F(telemetry_hub_test, local_task_counter_accumulates) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_task_local();
        v_hub.record_task_local();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_tasks_started_total);
    }

    TEST_F(telemetry_hub_test, snapshot_reports_set_worker_id_when_runtime_ready) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 7, 1, /*p_start_pump_thread=*/false);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(7u, v_frame.m_worker_id);
    }

    TEST_F(telemetry_hub_test, latest_worker_frame_returns_nullopt_for_non_center_role) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 5, 8, /*p_start_pump_thread=*/false);
        EXPECT_FALSE(v_hub.latest_worker_frame(0).has_value());
        EXPECT_FALSE(v_hub.latest_worker_frame(5).has_value());
    }

    TEST_F(telemetry_hub_test, manual_tick_publishes_frame_into_aggregator) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);

        v_hub.record_send(0, 64);
        v_hub.tick_if_due();

        const auto v_frame = v_hub.latest_worker_frame(0);
        ASSERT_TRUE(v_frame.has_value());
        EXPECT_EQ(0u, v_frame->m_worker_id);
        EXPECT_EQ(1u, v_frame->m_tasks_sent_total);
        ASSERT_EQ(1u, v_frame->m_edges_out.size());
        expect_edge(v_frame->m_edges_out[0], 0, 64, 1);
    }

    TEST_F(telemetry_hub_test, pump_thread_drives_periodic_publish_in_mt_mode) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        v_hub.set_worker_interval_ms(50);

        for (int v_i = 0; v_i < 100; ++v_i)
            v_hub.record_task_local();

        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        const auto v_frame = v_hub.latest_worker_frame(0);
        ASSERT_TRUE(v_frame.has_value());
        EXPECT_EQ(100u, v_frame->m_tasks_started_total);
    }

    TEST_F(telemetry_hub_test, concurrent_record_send_is_lossless) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 1, 2, /*p_start_pump_thread=*/false);

        constexpr unsigned k_threads = 8;
        constexpr unsigned k_per_thread = 1000;

        std::vector<std::thread> v_threads;
        v_threads.reserve(k_threads);
        for (unsigned v_t = 0; v_t < k_threads; ++v_t) {
            v_threads.emplace_back([&v_hub] {
                for (unsigned v_i = 0; v_i < k_per_thread; ++v_i) {
                    v_hub.record_send(1, 10);
                }
            });
        }
        for (auto& v_th: v_threads)
            v_th.join();

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(static_cast<std::uint64_t>(k_threads) * k_per_thread, v_frame.m_tasks_sent_total);
        ASSERT_EQ(1u, v_frame.m_edges_out.size());
        expect_edge(v_frame.m_edges_out[0], 1, static_cast<std::uint64_t>(k_threads) * k_per_thread * 10, static_cast<std::uint64_t>(k_threads) * k_per_thread);
    }

} // namespace
