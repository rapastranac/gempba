#ifndef GEMPBA_TELEMETRY_SCHEDULER_CLOCK_HPP
#define GEMPBA_TELEMETRY_SCHEDULER_CLOCK_HPP

#include <algorithm>
#include <array>
#include <cstdint>
#include <gempba/telemetry/frames.hpp>
#include <limits>

namespace gempba::telemetry {

    /**
     * @brief Turns a rank's scheduler events, stamped in microseconds, into the scheduler times of a @ref worker_frame.
     *
     * Times accrue only between @ref on_run_start and @ref on_run_end. Available-rank time accrues only while the
     * coordinator queue is empty: the centralized coordinator hands a task to every available rank while its queue holds any.
     * Not thread-safe.
     */
    class scheduler_clock {
    public:
        scheduler_clock() { m_assigned_at.fill(k_unassigned); }

        void on_run_start(const std::uint64_t p_now) {
            m_in_run = true;
            m_run_start = p_now;
            m_since = p_now;
        }

        void on_run_end(const std::uint64_t p_now) {
            if (!m_in_run) {
                return;
            }
            accrue(p_now);
            m_phase = phase::NONE;
            m_in_run = false;
        }

        void on_worker_waiting(const std::uint64_t p_now) {
            if (!m_in_run) {
                return;
            }
            accrue(p_now);
            if (m_phase == phase::RUNNING) {
                m_last_task_ms = (p_now - m_run_start) / 1000;
            }
            m_phase = phase::WAITING;
        }

        void on_worker_running(const std::uint64_t p_now) {
            if (!m_in_run) {
                return;
            }
            accrue(p_now);
            if (!m_task_seen) {
                m_task_seen = true;
                m_first_task_ms = (p_now - m_run_start) / 1000;
            }
            m_phase = phase::RUNNING;
        }

        void on_assignment(const std::uint32_t p_worker_id, const std::uint64_t p_now) {
            if (p_worker_id < MAX_WORKERS) {
                m_assigned_at[p_worker_id] = p_now;
            }
        }

        void on_assignment_accepted(const std::uint32_t p_worker_id, const std::uint64_t p_now) {
            if (p_worker_id >= MAX_WORKERS || m_assigned_at[p_worker_id] == k_unassigned) {
                return;
            }
            const std::uint64_t v_latency = p_now - m_assigned_at[p_worker_id];
            m_assigned_at[p_worker_id] = k_unassigned;
            m_assign_latency_total += v_latency;
            m_assign_latency_max = std::max(m_assign_latency_max, v_latency);
            ++m_assign_count;
        }

        void on_coordinator_queue(const std::uint32_t p_available_ranks, const bool p_queue_empty, const std::uint64_t p_now) {
            if (!m_in_run) {
                return;
            }
            accrue(p_now);
            m_available_ranks = p_available_ranks;
            m_queue_empty = p_queue_empty;
        }

        void on_coordinator_full(const std::uint64_t p_now) {
            if (!m_in_run || m_coordinator_full) {
                return;
            }
            accrue(p_now);
            m_coordinator_full = true;
        }

        void on_global_score_improved(const std::uint64_t p_now) {
            if (m_in_run) {
                m_last_score_improvement_ms = (p_now - m_run_start) / 1000;
            }
        }

        void on_coordinator_free(const std::uint64_t p_now) {
            if (!m_in_run || !m_coordinator_full) {
                return;
            }
            accrue(p_now);
            m_coordinator_full = false;
        }

        void fill(worker_frame& p_out, const std::uint64_t p_now) const {
            const std::uint64_t v_open = m_in_run ? p_now - m_since : 0;

            p_out.m_sched_running_microseconds = m_running + (m_phase == phase::RUNNING ? v_open : 0);
            p_out.m_sched_waiting_microseconds = m_waiting + (m_phase == phase::WAITING ? v_open : 0);
            p_out.m_first_task_ms = m_first_task_ms;
            p_out.m_last_task_ms = m_last_task_ms;
            p_out.m_assign_latency_microseconds_total = m_assign_latency_total;
            p_out.m_assign_count = m_assign_count;
            p_out.m_assign_latency_microseconds_max = m_assign_latency_max;
            p_out.m_available_microseconds_queue_empty = m_available_queue_empty + (m_queue_empty ? m_available_ranks * v_open : 0);
            p_out.m_coordinator_full_microseconds = m_coordinator_full_time + (m_coordinator_full ? v_open : 0);
            p_out.m_last_score_improvement_ms = m_last_score_improvement_ms;
        }

    private:
        enum class phase : std::uint8_t { NONE, WAITING, RUNNING };

        static constexpr std::uint64_t k_unassigned = std::numeric_limits<std::uint64_t>::max();

        void accrue(const std::uint64_t p_now) {
            const std::uint64_t v_elapsed = p_now - m_since;
            if (m_phase == phase::RUNNING) {
                m_running += v_elapsed;
            } else if (m_phase == phase::WAITING) {
                m_waiting += v_elapsed;
            }
            if (m_queue_empty) {
                m_available_queue_empty += m_available_ranks * v_elapsed;
            }
            if (m_coordinator_full) {
                m_coordinator_full_time += v_elapsed;
            }
            m_since = p_now;
        }

        bool m_in_run = false;
        std::uint64_t m_run_start = 0;
        std::uint64_t m_since = 0;

        phase m_phase = phase::NONE;
        bool m_task_seen = false;
        std::uint64_t m_running = 0;
        std::uint64_t m_waiting = 0;
        std::uint64_t m_first_task_ms = 0;
        std::uint64_t m_last_task_ms = 0;

        std::array<std::uint64_t, MAX_WORKERS> m_assigned_at{};
        std::uint64_t m_assign_latency_total = 0;
        std::uint64_t m_assign_latency_max = 0;
        std::uint64_t m_assign_count = 0;

        std::uint64_t m_available_ranks = 0;
        bool m_queue_empty = true;
        std::uint64_t m_available_queue_empty = 0;

        bool m_coordinator_full = false;
        std::uint64_t m_coordinator_full_time = 0;

        std::uint64_t m_last_score_improvement_ms = 0;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_SCHEDULER_CLOCK_HPP
