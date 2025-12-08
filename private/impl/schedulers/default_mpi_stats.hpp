/*
* MIT License
 *
 * Copyright (c) 2025. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef DEFAULT_MPI_STATS_HPP
#define DEFAULT_MPI_STATS_HPP

#include <compare>
#include <memory>
#include <tuple>

#include <gempba/stats/stats.hpp>
#include <gempba/stats/stats_visitor.hpp>

namespace gempba {
    class default_mpi_stats final : public stats {
        /**
         * Trivially copyable structure for serialization purposes. Should mirror the public members of stats_impl.
         */
        struct internal_data {
            int m_rank;
            size_t m_received_task_count;
            size_t m_sent_task_count;
            size_t m_total_requested_tasks;
            size_t m_total_thread_requests;
            double m_idle_time;
            double m_elapsed_time;
        };

    public:
        int m_rank;
        size_t m_received_task_count = 0;
        size_t m_sent_task_count = 0;
        size_t m_total_requested_tasks = 0;
        size_t m_total_thread_requests = 0;
        double m_idle_time = 0.0;
        double m_elapsed_time = 0.0;

        // Original constructor
        explicit default_mpi_stats(const int p_rank) :
            m_rank(p_rank) {
        }

        default_mpi_stats(const default_mpi_stats &p_other) = default;

        default_mpi_stats(default_mpi_stats &&p_other) noexcept = default;

        ~default_mpi_stats() override = default;

        default_mpi_stats &operator=(const default_mpi_stats &p_other) = default;

        default_mpi_stats &operator=(default_mpi_stats &&p_other) noexcept = default;

        friend bool operator==(const default_mpi_stats &p_lhs, const default_mpi_stats &p_rhs) {
            return p_lhs.m_rank == p_rhs.m_rank
                   && p_lhs.m_received_task_count == p_rhs.m_received_task_count
                   && p_lhs.m_sent_task_count == p_rhs.m_sent_task_count
                   && p_lhs.m_total_requested_tasks == p_rhs.m_total_requested_tasks
                   && p_lhs.m_total_thread_requests == p_rhs.m_total_thread_requests
                   && p_lhs.m_idle_time == p_rhs.m_idle_time
                   && p_lhs.m_elapsed_time == p_rhs.m_elapsed_time;
        }

        friend bool operator!=(const default_mpi_stats &p_lhs, const default_mpi_stats &p_rhs) {
            return !(p_lhs == p_rhs);
        }

        auto operator<=>(const default_mpi_stats &p_other) const {
            auto v_lhs = std::tie(m_rank, m_received_task_count, m_sent_task_count,
                                  m_total_requested_tasks, m_total_thread_requests,
                                  m_idle_time, m_elapsed_time);
            auto v_rhs = std::tie(p_other.m_rank, p_other.m_received_task_count, p_other.m_sent_task_count,
                                  p_other.m_total_requested_tasks, p_other.m_total_thread_requests,
                                  p_other.m_idle_time, p_other.m_elapsed_time);
            return v_lhs <=> v_rhs;
        }


        [[nodiscard]] task_packet serialize() const override {
            const internal_data v_data{
                    m_rank,
                    m_received_task_count,
                    m_sent_task_count,
                    m_total_requested_tasks,
                    m_total_thread_requests,
                    m_idle_time,
                    m_elapsed_time
            };

            return task_packet{reinterpret_cast<const char *>(&v_data), sizeof(internal_data)};
        }

        [[nodiscard]] std::vector<std::string> labels() const override {
            return {
                    "rank",
                    "received_task_count",
                    "sent_task_count",
                    "total_requested_tasks",
                    "idle_time",
                    "elapsed_time"
            };
        }

        static default_mpi_stats from_packet(const task_packet &p_packet) {
            if (p_packet.size() != sizeof(internal_data)) {
                throw std::invalid_argument("Invalid packet size for stats deserialization");
            }

            const auto *v_data = reinterpret_cast<const internal_data *>(p_packet.data());

            default_mpi_stats v_result(v_data->m_rank);
            v_result.m_received_task_count = v_data->m_received_task_count;
            v_result.m_sent_task_count = v_data->m_sent_task_count;
            v_result.m_total_requested_tasks = v_data->m_total_requested_tasks;
            v_result.m_total_thread_requests = v_data->m_total_thread_requests;
            v_result.m_idle_time = v_data->m_idle_time;
            v_result.m_elapsed_time = v_data->m_elapsed_time;

            return v_result;
        }

        void visit(const std::function<void(const std::string &, std::any &&)> p_visitor) const override {
            p_visitor("rank", std::make_any<int>(m_rank));
            p_visitor("received_task_count", std::make_any<std::size_t>(m_received_task_count));
            p_visitor("sent_task_count", std::make_any<std::size_t>(m_sent_task_count));
            p_visitor("total_requested_tasks", std::make_any<std::size_t>(m_total_requested_tasks));
            p_visitor("total_thread_requests", std::make_any<std::size_t>(m_total_thread_requests));
            p_visitor("idle_time", std::make_any<double>(m_idle_time));
            p_visitor("elapsed_time", std::make_any<double>(m_elapsed_time));
        }

        void visit(stats_visitor *const p_visitor) const override {
            p_visitor->visit(*this);
        }
    };
}

#endif //DEFAULT_MPI_STATS_HPP
