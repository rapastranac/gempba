/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
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

#if defined(_WIN32)
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
#endif

#include "leaf_count.hpp"

#include <gempba/defaults/default_mpi_stats_visitor.hpp>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/frames.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <mpi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

/**
 * Runs the leaf count and checks what telemetry reports about it across ranks: each rank's own frame, totals
 * gathered to rank 0, and the center's final broadcast as a dashboard connected to its TCP port receives it.
 * Every check compares totals after the run; none depends on timing.
 *
 * Usage: telemetry_stress <depth> <semi_centralized|centralized>
 */

namespace {
#if defined(_WIN32)
    using socket_t = SOCKET;
    constexpr socket_t k_invalid_socket = INVALID_SOCKET;
    void close_socket(const socket_t p_socket) { closesocket(p_socket); }
#else
    using socket_t = int;
    constexpr socket_t k_invalid_socket = -1;
    void close_socket(const socket_t p_socket) { ::close(p_socket); }
#endif

    sockaddr_in loopback_address(const std::uint16_t p_port) {
        sockaddr_in v_address{};
        v_address.sin_family = AF_INET;
        v_address.sin_port = htons(p_port);
        v_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return v_address;
    }

    std::uint16_t free_loopback_port() {
        const socket_t v_socket = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in v_address = loopback_address(0);
        if (::bind(v_socket, reinterpret_cast<sockaddr*>(&v_address), sizeof(v_address)) != 0) {
            close_socket(v_socket);
            return 0;
        }
#if defined(_WIN32)
        int v_length = sizeof(v_address);
#else
        socklen_t v_length = sizeof(v_address);
#endif
        ::getsockname(v_socket, reinterpret_cast<sockaddr*>(&v_address), &v_length);
        close_socket(v_socket);
        return ntohs(v_address.sin_port);
    }

    socket_t connect_loopback(const std::uint16_t p_port) {
        const socket_t v_socket = ::socket(AF_INET, SOCK_STREAM, 0);
        const sockaddr_in v_address = loopback_address(p_port);
        if (::connect(v_socket, reinterpret_cast<const sockaddr*>(&v_address), sizeof(v_address)) != 0) {
            close_socket(v_socket);
            return k_invalid_socket;
        }
#if defined(_WIN32)
        const DWORD v_timeout_ms = 200;
        setsockopt(v_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v_timeout_ms), sizeof(v_timeout_ms));
#else
        timeval v_timeout{0, 200'000};
        setsockopt(v_socket, SOL_SOCKET, SO_RCVTIMEO, &v_timeout, sizeof(v_timeout));
#endif
        return v_socket;
    }

    std::string read_until_closed(const socket_t p_socket, const std::chrono::seconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        std::string v_received;
        char v_chunk[4096];
        while (std::chrono::steady_clock::now() < v_deadline) {
            const auto v_count = ::recv(p_socket, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
            if (v_count == 0) {
                break;
            }
            if (v_count > 0) {
                v_received.append(v_chunk, static_cast<std::size_t>(v_count));
            }
        }
        return v_received;
    }

    std::string last_complete_line(const std::string& p_stream) {
        const auto v_end = p_stream.rfind('\n');
        if (v_end == std::string::npos) {
            return {};
        }
        const std::string v_complete = p_stream.substr(0, v_end);
        const auto v_previous = v_complete.rfind('\n');
        return v_previous == std::string::npos ? v_complete : v_complete.substr(v_previous + 1);
    }

    std::size_t count_of(const std::string_view p_text, const std::string_view p_needle) {
        std::size_t v_count = 0;
        for (auto v_at = p_text.find(p_needle); v_at != std::string_view::npos; v_at = p_text.find(p_needle, v_at + p_needle.size())) {
            ++v_count;
        }
        return v_count;
    }

    std::uint64_t sum_of(const std::string_view p_text, const std::string_view p_key) {
        const std::string v_needle = "\"" + std::string(p_key) + "\":";
        std::uint64_t v_sum = 0;
        for (auto v_at = p_text.find(v_needle); v_at != std::string_view::npos; v_at = p_text.find(v_needle, v_at + 1)) {
            v_sum += std::strtoull(std::string(p_text.substr(v_at + v_needle.size(), 24)).c_str(), nullptr, 10);
        }
        return v_sum;
    }

    class checks {
    public:
        explicit checks(const int p_rank) : m_rank(p_rank) {}

        void expect(const bool p_holds, const std::string_view p_what) {
            if (!p_holds) {
                std::cerr << "rank " << m_rank << ": " << p_what << '\n';
                ++m_failures;
            }
        }

        [[nodiscard]] int failures() const { return m_failures; }

    private:
        int m_rank;
        int m_failures = 0;
    };

    std::uint64_t edge_bytes(const gempba::telemetry::worker_frame& p_frame) {
        std::uint64_t v_sum = 0;
        for (const auto& v_edge: p_frame.m_edges_out) {
            v_sum += v_edge.m_bytes;
        }
        return v_sum;
    }
} // namespace

int main(const int p_argc, char* p_argv[]) {
    if (p_argc != 3) {
        std::cerr << "usage: telemetry_stress <depth> <semi_centralized|centralized>\n";
        return 2;
    }
    const int v_max_depth = std::stoi(p_argv[1]);
    const std::string_view v_topology_name = p_argv[2];

#if defined(_WIN32)
    WSADATA v_wsa{};
    WSAStartup(MAKEWORD(2, 2), &v_wsa);
#endif
    const std::uint16_t v_port = free_loopback_port();
    gempba::telemetry::configure_port(v_port);

    gempba::scheduler* v_scheduler = gempba::create_scheduler(leaf_count::parse_topology(v_topology_name));
    const int v_rank = v_scheduler->rank_me();
    const int v_world_size = v_scheduler->world_size();
    checks v_checks(v_rank);

    socket_t v_dashboard = k_invalid_socket;
    std::string v_dashboard_stream;
    std::thread v_dashboard_reader;
    if (v_rank == 0) {
        v_dashboard = connect_loopback(v_port);
        v_checks.expect(v_dashboard != k_invalid_socket, "could not connect to the center's telemetry port");
        v_checks.expect(gempba::telemetry::annotate_run("test", "telemetry_stress"), "the run annotation was refused");
        if (v_dashboard != k_invalid_socket) {
            v_dashboard_reader = std::thread([&] { v_dashboard_stream = read_until_closed(v_dashboard, std::chrono::seconds(60)); });
        }
    }

    const auto v_counter = leaf_count::run(*v_scheduler, v_max_depth);
    v_scheduler->barrier();

    gempba::telemetry::worker_frame v_frame;
    gempba::telemetry::get()->snapshot_worker_frame(v_frame);
    v_checks.expect(edge_bytes(v_frame) == v_frame.m_bytes_sent_total, "summed edges_out bytes differ from bytes_sent_total");
    if (v_rank != 0) {
        const auto v_elapsed_us = static_cast<long long>(v_scheduler->elapsed_time() * 1e6);
        const auto v_accounted_us = static_cast<long long>(v_frame.m_sched_running_microseconds + v_frame.m_sched_waiting_microseconds);
        v_checks.expect(std::llabs(v_accounted_us - v_elapsed_us) <= 50'000, "running + waiting does not cover the run time");
        v_checks.expect(v_frame.m_tasks_recv_total == 0 || v_frame.m_sched_running_microseconds > 0, "a worker that received tasks reports no running time");
    }

    constexpr std::size_t k_fields = 6;
    const std::array<unsigned long long, k_fields> v_local{v_frame.m_tasks_sent_total, v_frame.m_tasks_recv_total,     v_frame.m_bytes_sent_total,
                                                           v_frame.m_bytes_recv_total, v_frame.m_score_proposals_sent, static_cast<unsigned long long>(v_counter->leaves())};
    std::vector<unsigned long long> v_all(k_fields * static_cast<std::size_t>(v_world_size));
    MPI_Gather(v_local.data(), static_cast<int>(k_fields), MPI_UNSIGNED_LONG_LONG, v_all.data(), static_cast<int>(k_fields), MPI_UNSIGNED_LONG_LONG, 0, MPI_COMM_WORLD);

    if (v_rank == 0) {
        std::array<unsigned long long, k_fields> v_totals{};
        unsigned long long v_worker_proposals = 0;
        for (std::size_t v_r = 0; std::cmp_less(v_r, v_world_size); ++v_r) {
            for (std::size_t v_f = 0; v_f < k_fields; ++v_f) {
                v_totals[v_f] += v_all[v_r * k_fields + v_f];
            }
            if (v_r != 0) {
                v_worker_proposals += v_all[v_r * k_fields + 4];
            }
        }
        v_checks.expect(v_totals[5] == (1ULL << v_max_depth), "the leaf count is wrong");
        v_checks.expect(v_totals[0] == v_totals[1], "tasks sent differ from tasks received across ranks");
        v_checks.expect(v_totals[2] == v_totals[3], "bytes sent differ from bytes received across ranks");
        v_checks.expect(v_worker_proposals == v_frame.m_center_score_proposals_total, "workers' score proposals differ from those the center received");

        gempba::default_mpi_stats_visitor v_stats;
        v_scheduler->get_stats()->visit(&v_stats);
        const std::uint64_t v_kinds = v_frame.m_center_state_msgs_total + v_frame.m_tasks_recv_total + v_frame.m_tasks_sent_total + v_frame.m_center_score_proposals_total +
                                      v_frame.m_center_score_broadcasts_total;
        v_checks.expect(v_stats.m_center_messages_handled == v_kinds, "center_messages_handled differs from the center's per-kind counters");

        std::cout << v_topology_name << " ranks=" << v_world_size << " tasks=" << v_totals[0] << " bytes=" << v_totals[2] << " proposals=" << v_worker_proposals
                  << " center_messages=" << v_stats.m_center_messages_handled << '\n';
    }

    v_scheduler->barrier();
    const int v_shutdown_code = gempba::shutdown();

    if (v_rank == 0 && v_dashboard_reader.joinable()) {
        v_dashboard_reader.join();
        close_socket(v_dashboard);
        const std::string v_line = last_complete_line(v_dashboard_stream);
        const auto v_run_id_at = v_line.find(R"("run_id":")");
        v_checks.expect(v_run_id_at != std::string::npos && v_line.size() > v_run_id_at + 46 && v_line[v_run_id_at + 46] == '"', "the final broadcast has no 36-character run_id");
        v_checks.expect(v_line.find(R"("scheduler":")" + std::string(v_topology_name) + '"') != std::string::npos, "the final broadcast names another scheduler");
        v_checks.expect(v_line.find(R"("load_balancer":"quasi_horizontal")") != std::string::npos, "the final broadcast names another load balancer");
        v_checks.expect(v_line.find(R"("annotations":{"test":"telemetry_stress"})") != std::string::npos, "the final broadcast lost the run annotation");
        v_checks.expect(count_of(v_line, R"("role":"center")") == 1, "the final broadcast does not mark exactly one center");
        v_checks.expect(count_of(v_line, R"("role":"worker")") == static_cast<std::size_t>(v_world_size - 1), "the final broadcast does not mark every other rank a worker");
        for (int v_r = 0; v_r < v_world_size; ++v_r) {
            v_checks.expect(v_line.find("{\"worker_id\":" + std::to_string(v_r) + ",\"seq_no\":") != std::string::npos, "the final broadcast lacks rank " + std::to_string(v_r) + "'s frame");
        }
        const std::size_t v_hosts = count_of(v_line, R"({"hostname":)");
        v_checks.expect(v_hosts > 0 && count_of(v_line, R"({"sentinel_worker_id":)") == v_hosts, "the final broadcast does not carry one node frame per host");
        const std::uint64_t v_edges_bytes = sum_of(v_line, "bytes");
        v_checks.expect(v_edges_bytes > 0 && v_edges_bytes == sum_of(v_line, "bytes_sent_total"), "the final broadcast's edges do not add up to the bytes sent");
    }
#if defined(_WIN32)
    WSACleanup();
#endif

    if (v_checks.failures() != 0) {
        return 1;
    }
    return v_shutdown_code;
}
