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
#ifndef GEMPBA_DEFAULT_MPI_STATS_VISITOR_HPP
#define GEMPBA_DEFAULT_MPI_STATS_VISITOR_HPP
#include <cstddef>
#include <memory>
#include <schedulers/api/stats.hpp>
#include <schedulers/api/stats_visitor.hpp>

namespace gempba {
    class default_mpi_stats_visitor final : public stats_visitor {

        default_mpi_stats_visitor() = default;

        friend std::unique_ptr<default_mpi_stats_visitor> create_default_mpi_stats_visitor();

    public:
        ~default_mpi_stats_visitor() override = default;

        std::size_t m_received_task_count{};
        std::size_t m_sent_task_count{};
        std::size_t m_total_requested_tasks{};
        std::size_t m_total_thread_requests{};
        double m_idle_time{};
        double m_elapsed_time{};


        void visit(const stats &p_stats) override {
            p_stats.visit([this](const std::string &p_key, std::any &&p_value) {
                if (p_key == "received_task_count") {
                    m_received_task_count = std::any_cast<std::size_t>(p_value);
                } else if (p_key == "sent_task_count") {
                    m_sent_task_count = std::any_cast<std::size_t>(p_value);
                } else if (p_key == "total_requested_tasks") {
                    m_total_requested_tasks = std::any_cast<std::size_t>(p_value);
                } else if (p_key == "total_thread_requests") {
                    m_total_thread_requests = std::any_cast<std::size_t>(p_value);
                } else if (p_key == "idle_time") {
                    m_idle_time = std::any_cast<double>(p_value);
                } else if (p_key == "elapsed_time") {
                    m_elapsed_time = std::any_cast<double>(p_value);
                }
            });
        }
    };

    inline std::unique_ptr<default_mpi_stats_visitor> create_default_mpi_stats_visitor() {
        return std::unique_ptr<default_mpi_stats_visitor>(new default_mpi_stats_visitor());
    }

}

#endif //GEMPBA_DEFAULT_MPI_STATS_VISITOR_HPP
