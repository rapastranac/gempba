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
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

/**
 * Runs the leaf count and checks what telemetry reports about it across ranks: each rank's own frame, totals
 * gathered to rank 0, and the aggregator's final broadcast as a dashboard connected to its TCP port receives it.
 * Then the aggregator pauses telemetry and resumes it, each step between barriers: the pause must reach every worker
 * and stop all frames, a snapshot requested while paused must bring every rank's frame to the aggregator and the
 * dashboard, and after the resume the aggregator must receive the tasks the workers counted while paused.
 * Every check compares totals or waits for a state with a generous deadline; only the snapshot needs every worker,
 * ticking every few milliseconds, to answer within the aggregator's one-second wait.
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

    void read_until_closed(const socket_t p_socket, const std::chrono::seconds p_timeout, std::string& p_received, std::mutex& p_received_mtx) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        char v_chunk[4096];
        while (std::chrono::steady_clock::now() < v_deadline) {
            const auto v_count = ::recv(p_socket, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
            if (v_count == 0) {
                break;
            }
            if (v_count > 0) {
                const std::scoped_lock v_lock(p_received_mtx);
                p_received.append(v_chunk, static_cast<std::size_t>(v_count));
            }
        }
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

    std::uint64_t edge_bytes(const gempba::telemetry::rank_frame& p_frame) {
        std::uint64_t v_sum = 0;
        for (const auto& v_edge: p_frame.m_edges_out) {
            v_sum += v_edge.m_bytes;
        }
        return v_sum;
    }

    bool tick_until(gempba::telemetry::telemetry_hub& p_hub, const std::function<bool()>& p_done) {
        const auto v_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!p_done() && std::chrono::steady_clock::now() < v_deadline) {
            p_hub.tick_if_due();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return p_done();
    }

    gempba::telemetry::rank_frame own_frame(gempba::telemetry::telemetry_hub& p_hub) {
        gempba::telemetry::rank_frame v_frame;
        p_hub.snapshot_rank_frame(v_frame);
        return v_frame;
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
    std::mutex v_dashboard_mtx;
    std::thread v_dashboard_reader;
    if (v_rank == 0) {
        v_dashboard = connect_loopback(v_port);
        v_checks.expect(v_dashboard != k_invalid_socket, "could not connect to the aggregator's telemetry port");
        v_checks.expect(gempba::telemetry::annotate_run("test", "telemetry_stress"), "the run annotation was refused");
        if (v_dashboard != k_invalid_socket) {
            v_dashboard_reader = std::thread([&] { read_until_closed(v_dashboard, std::chrono::seconds(60), v_dashboard_stream, v_dashboard_mtx); });
        }
    }

    const auto v_counter = leaf_count::run(*v_scheduler, v_max_depth);
    v_scheduler->barrier();

    gempba::telemetry::rank_frame v_frame;
    gempba::telemetry::get()->snapshot_rank_frame(v_frame);
    v_checks.expect(edge_bytes(v_frame) == v_frame.m_bytes_sent_total, "summed edges_out bytes differ from bytes_sent_total");
    if (v_rank != 0) {
        const auto v_elapsed_us = static_cast<long long>(v_scheduler->elapsed_time() * 1e6);
        const auto v_accounted_us = static_cast<long long>(v_frame.m_sched_running_microseconds + v_frame.m_sched_waiting_microseconds);
        v_checks.expect(std::llabs(v_accounted_us - v_elapsed_us) <= 50'000, "running + waiting does not cover the run time");
        v_checks.expect(v_frame.m_tasks_recv_total == 0 || v_frame.m_sched_running_microseconds > 0, "a worker that received tasks reports no running time");
    }

    constexpr std::size_t k_fields = 7;
    const std::array<unsigned long long, k_fields> v_local{v_frame.m_tasks_sent_total,   v_frame.m_tasks_recv_total,     v_frame.m_bytes_sent_total,
                                                           v_frame.m_bytes_recv_total,   v_frame.m_score_proposals_sent, static_cast<unsigned long long>(v_counter->leaves()),
                                                           v_frame.m_tasks_started_total};
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
        v_checks.expect(v_worker_proposals == v_frame.m_coordinator_score_proposals_total, "workers' score proposals differ from those the center received");

        gempba::default_mpi_stats_visitor v_stats;
        v_scheduler->get_stats()->visit(&v_stats);
        const std::uint64_t v_kinds = v_frame.m_coordinator_state_msgs_total + v_frame.m_tasks_recv_total + v_frame.m_tasks_sent_total + v_frame.m_coordinator_score_proposals_total +
                                      v_frame.m_coordinator_score_broadcasts_total;
        v_checks.expect(v_stats.m_coordinator_messages_handled == v_kinds, "coordinator_messages_handled differs from the coordinator's per-kind counters");

        std::cout << v_topology_name << " ranks=" << v_world_size << " tasks=" << v_totals[0] << " bytes=" << v_totals[2] << " proposals=" << v_worker_proposals
                  << " coordinator_messages=" << v_stats.m_coordinator_messages_handled << '\n';
    }

    gempba::telemetry::telemetry_hub& v_hub = *gempba::telemetry::get();
    const std::uint32_t v_interval_ms = v_hub.interval_ms();
    if (v_rank == 0) {
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);
        v_hub.tick_if_due();
    } else {
        v_checks.expect(tick_until(v_hub, [&] { return v_hub.paused(); }), "the pause did not reach this worker");
    }
    v_scheduler->barrier();

    constexpr unsigned long long k_paused_tasks = 7;
    const std::uint32_t v_paused_seq_no = own_frame(v_hub).m_seq_no;
    std::vector<std::uint32_t> v_ingested_seq_nos(static_cast<std::size_t>(v_world_size));
    if (v_rank == 0) {
        for (int v_r = 1; v_r < v_world_size; ++v_r) {
            const auto v_latest = v_hub.latest_rank_frame(static_cast<std::uint32_t>(v_r));
            v_ingested_seq_nos[static_cast<std::size_t>(v_r)] = v_latest ? v_latest->m_seq_no : 0;
        }
    } else {
        for (unsigned long long v_i = 0; v_i < k_paused_tasks; ++v_i) {
            v_hub.record_task_local();
        }
    }
    const auto v_paused_until = std::chrono::steady_clock::now() + 2 * std::chrono::milliseconds(v_interval_ms);
    const bool v_paused_window_done = tick_until(v_hub, [&] {
        if (std::chrono::steady_clock::now() < v_paused_until) {
            return false;
        }
        const std::scoped_lock v_lock(v_dashboard_mtx);
        return v_rank != 0 || v_dashboard_stream.find(R"("paused":true,"final":false})") != std::string::npos;
    });
    v_checks.expect(own_frame(v_hub).m_seq_no == v_paused_seq_no, "a paused rank published a frame");
    if (v_rank == 0) {
        v_checks.expect(v_paused_window_done, "the dashboard received no paused broadcast");
        for (int v_r = 1; v_r < v_world_size; ++v_r) {
            const auto v_latest = v_hub.latest_rank_frame(static_cast<std::uint32_t>(v_r));
            v_checks.expect((v_latest ? v_latest->m_seq_no : 0) == v_ingested_seq_nos[static_cast<std::size_t>(v_r)],
                            "the paused aggregator ingested a frame from rank " + std::to_string(v_r));
        }
    }
    v_scheduler->barrier();

    constexpr unsigned long long k_tasks_after_snapshot = 5;
    if (v_rank == 0) {
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::SNAPSHOT, 0);
        const std::uint32_t v_snapshot_id = v_hub.snapshot_requested();
        const bool v_answered = tick_until(v_hub, [&] {
            for (std::size_t v_r = 0; std::cmp_less(v_r, v_world_size); ++v_r) {
                const auto v_latest = v_hub.latest_rank_frame(static_cast<std::uint32_t>(v_r));
                if (!v_latest || v_latest->m_snapshot_id != v_snapshot_id || (v_r != 0 && v_latest->m_tasks_started_total != v_all[v_r * k_fields + 6] + k_paused_tasks)) {
                    return false;
                }
            }
            return true;
        });
        v_checks.expect(v_snapshot_id == 1 && v_answered, "the paused aggregator lacks a rank's snapshot frame");
        const bool v_broadcast = tick_until(v_hub, [&] {
            const std::scoped_lock v_lock(v_dashboard_mtx);
            return v_dashboard_stream.find(R"("paused":true,"final":false,"topology":)") != std::string::npos;
        });
        v_checks.expect(v_broadcast, "the dashboard received no snapshot while paused");
        v_checks.expect(v_hub.paused(), "the snapshot ended the pause");
    } else {
        v_checks.expect(tick_until(v_hub, [&] { return own_frame(v_hub).m_snapshot_id != 0; }), "this worker did not answer the snapshot");
        for (unsigned long long v_i = 0; v_i < k_tasks_after_snapshot; ++v_i) {
            v_hub.record_task_local();
        }
    }
    v_scheduler->barrier();

    const std::uint32_t v_snapshot_seq_no = own_frame(v_hub).m_seq_no;
    if (v_rank == 0) {
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::RESUME, 0);
        v_hub.tick_if_due();
    } else {
        v_checks.expect(tick_until(v_hub, [&] { return !v_hub.paused() && own_frame(v_hub).m_seq_no != v_snapshot_seq_no; }),
                        "the resume did not reach this worker, or it published no frame after it");
    }
    v_scheduler->barrier();
    if (v_rank == 0) {
        const bool v_caught_up = tick_until(v_hub, [&] {
            for (std::size_t v_r = 1; std::cmp_less(v_r, v_world_size); ++v_r) {
                const auto v_latest = v_hub.latest_rank_frame(static_cast<std::uint32_t>(v_r));
                if (!v_latest || v_latest->m_tasks_started_total != v_all[v_r * k_fields + 6] + k_paused_tasks + k_tasks_after_snapshot) {
                    return false;
                }
            }
            return true;
        });
        v_checks.expect(v_caught_up, "after the resume the aggregator lacks the tasks the workers counted while paused");
    }
    v_checks.expect(v_hub.interval_ms() == v_interval_ms, "pausing and resuming changed the interval");

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
        v_checks.expect(v_line.find(R"("paused":false,"final":true,)") != std::string::npos, "the final broadcast is still paused or not marked final");
        v_checks.expect(count_of(v_line, R"("role":"coordinator")") == 1, "the final broadcast does not mark exactly one coordinator");
        v_checks.expect(count_of(v_line, R"("role":"worker")") == static_cast<std::size_t>(v_world_size - 1), "the final broadcast does not mark every other rank a worker");
        for (int v_r = 0; v_r < v_world_size; ++v_r) {
            v_checks.expect(v_line.find("{\"rank\":" + std::to_string(v_r) + ",\"seq_no\":") != std::string::npos, "the final broadcast lacks rank " + std::to_string(v_r) + "'s frame");
        }
        const std::size_t v_hosts = count_of(v_line, R"({"hostname":)");
        v_checks.expect(v_hosts > 0 && count_of(v_line, R"({"sentinel_rank":)") == v_hosts, "the final broadcast does not carry one node frame per host");
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
