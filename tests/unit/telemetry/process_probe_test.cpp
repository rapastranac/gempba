#include <chrono>
#include <gempba/telemetry/frames.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/process_probe.hpp>
#include <thread>

namespace {

    TEST(process_probe_test, first_sample_reports_zero_cpu) {
        gempba::telemetry::process_probe v_probe;
        gempba::telemetry::worker_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_FLOAT_EQ(0.0f, v_frame.m_process_cpu_pct);
    }

    TEST(process_probe_test, rss_is_non_zero_after_sample) {
        gempba::telemetry::process_probe v_probe;
        gempba::telemetry::worker_frame v_frame{};
        v_probe.sample(v_frame);
        EXPECT_GT(v_frame.m_process_rss_bytes, 0u);
    }

    TEST(process_probe_test, sample_does_not_touch_thread_count) {
        constexpr std::uint32_t k_sentinel = 0xDEADBEEFu;
        gempba::telemetry::process_probe v_probe;
        gempba::telemetry::worker_frame v_frame{};
        v_frame.m_process_threads = k_sentinel;
        v_probe.sample(v_frame);
        EXPECT_EQ(k_sentinel, v_frame.m_process_threads);
    }

    TEST(process_probe_test, busy_loop_between_samples_reports_non_zero_cpu) {
        gempba::telemetry::process_probe v_probe;
        gempba::telemetry::worker_frame v_frame{};
        v_probe.sample(v_frame); // baseline

        const auto v_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
        volatile std::uint64_t v_acc = 0;
        while (std::chrono::steady_clock::now() < v_deadline) {
            for (int v_i = 0; v_i < 10000; ++v_i)
                v_acc += static_cast<std::uint64_t>(v_i);
        }

        v_probe.sample(v_frame);
        EXPECT_GT(v_frame.m_process_cpu_pct, 5.0f);
    }

} // namespace
