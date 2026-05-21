#include <atomic>
#include <chrono>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

namespace {

    class telemetry_hub_test : public ::testing::Test {
    protected:
        void TearDown() override { gempba::telemetry::uninstall(); }
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
        EXPECT_EQ(0u, v_frame.m_tasks_local_total);
        EXPECT_EQ(0u, v_frame.m_tasks_sent_total);
        EXPECT_EQ(0u, v_frame.m_tasks_recv_total);
        EXPECT_EQ(0u, v_frame.m_tasks_running);
        EXPECT_EQ(0u, v_frame.m_scheduler_pending_count);
        EXPECT_EQ(0u, v_frame.m_idle_microseconds_per_worker);
        EXPECT_GT(v_frame.m_worker_local_ms, 0u);
    }

    TEST_F(telemetry_hub_test, record_send_accumulates_per_destination_edges) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_send(2, 100);
        v_hub.record_send(2, 50);
        v_hub.record_send(5, 999);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);

        EXPECT_EQ(3u, v_frame.m_tasks_sent_total);
        EXPECT_EQ(150u, v_frame.m_edges_out[2].m_bytes);
        EXPECT_EQ(2u, v_frame.m_edges_out[2].m_count);
        EXPECT_EQ(999u, v_frame.m_edges_out[5].m_bytes);
        EXPECT_EQ(1u, v_frame.m_edges_out[5].m_count);
        EXPECT_EQ(0u, v_frame.m_edges_out[1].m_bytes);
    }

    TEST_F(telemetry_hub_test, record_send_to_out_of_range_id_still_bumps_total) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_send(gempba::telemetry::MAX_WORKERS + 10, 64);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(1u, v_frame.m_tasks_sent_total);
    }

    TEST_F(telemetry_hub_test, record_recv_increments_total_only) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        v_hub.record_recv(3, 1024);
        v_hub.record_recv(3, 2048);

        gempba::telemetry::worker_frame v_frame{};
        v_hub.snapshot_worker_frame(v_frame);
        EXPECT_EQ(2u, v_frame.m_tasks_recv_total);
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
        EXPECT_EQ(2u, v_frame.m_tasks_local_total);
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

        v_hub.record_send(2, 64);
        v_hub.tick_if_due();

        const auto v_frame = v_hub.latest_worker_frame(0);
        ASSERT_TRUE(v_frame.has_value());
        EXPECT_EQ(0u, v_frame->m_worker_id);
        EXPECT_EQ(1u, v_frame->m_tasks_sent_total);
        EXPECT_EQ(64u, v_frame->m_edges_out[2].m_bytes);
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
        EXPECT_EQ(100u, v_frame->m_tasks_local_total);
    }

    TEST_F(telemetry_hub_test, concurrent_record_send_is_lossless) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

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
        EXPECT_EQ(static_cast<std::uint64_t>(k_threads) * k_per_thread, v_frame.m_edges_out[1].m_count);
        EXPECT_EQ(static_cast<std::uint64_t>(k_threads) * k_per_thread * 10, v_frame.m_edges_out[1].m_bytes);
    }

} // namespace
