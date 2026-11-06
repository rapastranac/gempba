#include <gempba/telemetry/frames.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/scheduler_clock.hpp>

namespace {

    using gempba::telemetry::scheduler_clock;
    using gempba::telemetry::worker_frame;

    worker_frame fill_at(const scheduler_clock& p_clock, const std::uint64_t p_now) {
        worker_frame v_frame{};
        p_clock.fill(v_frame, p_now);
        return v_frame;
    }

    TEST(scheduler_clock_test, worker_running_and_waiting_add_up_to_the_run_time) {
        scheduler_clock v_clock;
        v_clock.on_run_start(1'000);
        v_clock.on_worker_waiting(1'000);
        v_clock.on_worker_running(3'000);
        v_clock.on_worker_waiting(6'000);
        v_clock.on_worker_running(10'000);
        v_clock.on_worker_waiting(16'000);
        v_clock.on_run_end(21'000);

        const worker_frame v_frame = fill_at(v_clock, 30'000);
        EXPECT_EQ(9'000u, v_frame.m_sched_running_microseconds);
        EXPECT_EQ(11'000u, v_frame.m_sched_waiting_microseconds);
        EXPECT_EQ(20'000u, v_frame.m_sched_running_microseconds + v_frame.m_sched_waiting_microseconds);
    }

    TEST(scheduler_clock_test, worker_task_times_are_relative_to_run_start) {
        scheduler_clock v_clock;
        v_clock.on_run_start(1'000);
        v_clock.on_worker_waiting(1'000);
        v_clock.on_worker_running(3'000);
        v_clock.on_worker_waiting(6'000);
        v_clock.on_worker_running(10'000);
        v_clock.on_worker_waiting(16'000);

        const worker_frame v_frame = fill_at(v_clock, 20'000);
        EXPECT_EQ(2u, v_frame.m_first_task_ms);
        EXPECT_EQ(15u, v_frame.m_last_task_ms);
    }

    TEST(scheduler_clock_test, live_frame_includes_the_open_interval) {
        scheduler_clock v_clock;
        v_clock.on_run_start(0);
        v_clock.on_worker_waiting(0);
        v_clock.on_worker_running(1'000);

        const worker_frame v_running = fill_at(v_clock, 4'000);
        EXPECT_EQ(3'000u, v_running.m_sched_running_microseconds);
        EXPECT_EQ(1'000u, v_running.m_sched_waiting_microseconds);

        v_clock.on_worker_waiting(5'000);
        const worker_frame v_waiting = fill_at(v_clock, 7'000);
        EXPECT_EQ(4'000u, v_waiting.m_sched_running_microseconds);
        EXPECT_EQ(3'000u, v_waiting.m_sched_waiting_microseconds);
    }

    TEST(scheduler_clock_test, nothing_accrues_outside_the_run) {
        scheduler_clock v_clock;
        v_clock.on_worker_running(1'000);
        v_clock.on_coordinator_queue(4, true, 1'000);
        EXPECT_EQ(0u, fill_at(v_clock, 5'000).m_sched_running_microseconds);
        EXPECT_EQ(0u, fill_at(v_clock, 5'000).m_available_microseconds_queue_empty);

        v_clock.on_run_start(5'000);
        v_clock.on_worker_waiting(5'000);
        v_clock.on_run_end(8'000);
        v_clock.on_worker_running(9'000);

        const worker_frame v_frame = fill_at(v_clock, 12'000);
        EXPECT_EQ(0u, v_frame.m_sched_running_microseconds);
        EXPECT_EQ(3'000u, v_frame.m_sched_waiting_microseconds);
        EXPECT_EQ(0u, v_frame.m_first_task_ms);
    }

    TEST(scheduler_clock_test, assignment_latency_sums_counts_and_keeps_the_max) {
        scheduler_clock v_clock;
        v_clock.on_assignment(1, 100);
        v_clock.on_assignment(2, 200);
        v_clock.on_assignment_accepted(1, 400);
        v_clock.on_assignment_accepted(2, 1'200);

        const worker_frame v_frame = fill_at(v_clock, 2'000);
        EXPECT_EQ(1'300u, v_frame.m_assign_latency_microseconds_total);
        EXPECT_EQ(2u, v_frame.m_assign_count);
        EXPECT_EQ(1'000u, v_frame.m_assign_latency_microseconds_max);
    }

    TEST(scheduler_clock_test, running_report_without_a_pending_assignment_is_ignored) {
        scheduler_clock v_clock;
        v_clock.on_assignment_accepted(3, 500);
        v_clock.on_assignment(3, 600);
        v_clock.on_assignment_accepted(3, 700);
        v_clock.on_assignment_accepted(3, 900);
        v_clock.on_assignment(gempba::telemetry::MAX_WORKERS, 1'000);
        v_clock.on_assignment_accepted(gempba::telemetry::MAX_WORKERS, 1'100);

        const worker_frame v_frame = fill_at(v_clock, 2'000);
        EXPECT_EQ(100u, v_frame.m_assign_latency_microseconds_total);
        EXPECT_EQ(1u, v_frame.m_assign_count);
    }

    TEST(scheduler_clock_test, available_time_accrues_only_while_the_coordinator_queue_is_empty) {
        scheduler_clock v_clock;
        v_clock.on_run_start(0);
        v_clock.on_coordinator_queue(2, true, 0);
        v_clock.on_coordinator_queue(3, false, 1'000);
        v_clock.on_coordinator_queue(1, true, 3'000);

        const worker_frame v_live = fill_at(v_clock, 3'500);
        EXPECT_EQ(2'500u, v_live.m_available_microseconds_queue_empty);

        v_clock.on_run_end(4'000);
        const worker_frame v_final = fill_at(v_clock, 9'000);
        EXPECT_EQ(3'000u, v_final.m_available_microseconds_queue_empty);
    }

    TEST(scheduler_clock_test, coordinator_full_time_spans_full_to_free_and_ignores_repeated_notices) {
        scheduler_clock v_clock;
        v_clock.on_run_start(0);
        v_clock.on_coordinator_free(500);
        v_clock.on_coordinator_full(1'000);
        v_clock.on_coordinator_full(2'000);
        v_clock.on_coordinator_free(4'000);
        v_clock.on_coordinator_free(5'000);
        EXPECT_EQ(3'000u, fill_at(v_clock, 5'500).m_coordinator_full_microseconds);

        v_clock.on_coordinator_full(6'000);
        EXPECT_EQ(4'500u, fill_at(v_clock, 7'500).m_coordinator_full_microseconds);

        v_clock.on_run_end(8'000);
        EXPECT_EQ(5'000u, fill_at(v_clock, 9'000).m_coordinator_full_microseconds);
    }

    TEST(scheduler_clock_test, coordinator_full_time_does_not_accrue_outside_the_run) {
        scheduler_clock v_clock;
        v_clock.on_coordinator_full(0);
        v_clock.on_run_start(1'000);
        EXPECT_EQ(0u, fill_at(v_clock, 3'000).m_coordinator_full_microseconds);
    }

    TEST(scheduler_clock_test, last_score_improvement_is_the_latest_relative_to_run_start) {
        scheduler_clock v_clock;
        v_clock.on_global_score_improved(500);
        EXPECT_EQ(0u, fill_at(v_clock, 600).m_last_score_improvement_ms);

        v_clock.on_run_start(1'000);
        v_clock.on_global_score_improved(4'000);
        v_clock.on_global_score_improved(9'000);
        EXPECT_EQ(8u, fill_at(v_clock, 10'000).m_last_score_improvement_ms);

        v_clock.on_run_end(12'000);
        v_clock.on_global_score_improved(15'000);
        EXPECT_EQ(8u, fill_at(v_clock, 16'000).m_last_score_improvement_ms);
    }

    TEST(scheduler_clock_test, coordinator_events_leave_worker_times_at_zero) {
        scheduler_clock v_clock;
        v_clock.on_run_start(0);
        v_clock.on_coordinator_queue(2, true, 0);
        v_clock.on_run_end(5'000);

        const worker_frame v_frame = fill_at(v_clock, 5'000);
        EXPECT_EQ(0u, v_frame.m_sched_running_microseconds);
        EXPECT_EQ(0u, v_frame.m_sched_waiting_microseconds);
        EXPECT_EQ(10'000u, v_frame.m_available_microseconds_queue_empty);
    }

} // namespace
