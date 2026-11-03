#include <chrono>
#include <cstring>
#include <functional>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/center_tcp_server.hpp>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <winsock2.h>
using socket_t = SOCKET;
static constexpr socket_t k_invalid = INVALID_SOCKET;
static void close_sock(socket_t p_s) { closesocket(p_s); }
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
using socket_t = int;
static constexpr socket_t k_invalid = -1;
static void close_sock(socket_t p_s) { ::close(p_s); }
#endif

namespace {

    class tcp_server_test : public ::testing::Test {
    protected:
#if defined(_WIN32)
        WSADATA m_wsa{};
        void SetUp() override { WSAStartup(MAKEWORD(2, 2), &m_wsa); }
        void TearDown() override {
            gempba::telemetry::uninstall();
            WSACleanup();
        }
#else
        void TearDown() override { gempba::telemetry::uninstall(); }
#endif
    };

    std::string read_one_line(socket_t p_s, std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        std::string v_buf;
        char v_chunk[1024];
        while (std::chrono::steady_clock::now() < v_deadline) {
#if defined(_WIN32)
            const int v_n = recv(p_s, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
#else
            const ssize_t v_n = recv(p_s, v_chunk, sizeof(v_chunk), 0);
#endif
            if (v_n > 0) {
                v_buf.append(v_chunk, v_chunk + v_n);
                const auto v_pos = v_buf.find('\n');
                if (v_pos != std::string::npos)
                    return v_buf.substr(0, v_pos);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return v_buf;
    }

    TEST_F(tcp_server_test, ephemeral_port_bind_and_broadcast_round_trip) {
        gempba::telemetry::telemetry_hub v_hub;
        // Don't auto-spawn the timer thread or the default-port TCP server —
        // the test stands up its own server on an ephemeral port.
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(0);

        gempba::telemetry::center_tcp_server v_server(0, v_hub); // 0 → OS picks port
        ASSERT_TRUE(v_server.start());
        const std::uint16_t v_port = v_server.bound_port();
        ASSERT_GT(v_port, 0u);

        // Drive at least one publish into the aggregator so the broadcast has
        // workers to report.
        v_hub.record_send(2, 64);
        v_hub.tick_if_due();

        socket_t v_client = ::socket(AF_INET, SOCK_STREAM, 0);
        ASSERT_NE(k_invalid, v_client);
        sockaddr_in v_addr{};
        v_addr.sin_family = AF_INET;
        v_addr.sin_port = htons(v_port);
        v_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ASSERT_EQ(0, ::connect(v_client, reinterpret_cast<sockaddr*>(&v_addr), sizeof(v_addr)));

        // Cap each blocking recv at 200 ms so the deadline check in
        // read_one_line actually has a chance to fire on platforms where
        // recv would otherwise wait indefinitely.
#if defined(_WIN32)
        DWORD v_recv_timeout_ms = 200;
        setsockopt(v_client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v_recv_timeout_ms), sizeof(v_recv_timeout_ms));
#else
        struct timeval v_recv_timeout{};
        v_recv_timeout.tv_sec = 0;
        v_recv_timeout.tv_usec = 200'000;
        setsockopt(v_client, SOL_SOCKET, SO_RCVTIMEO, &v_recv_timeout, sizeof(v_recv_timeout));
#endif

        const std::string v_line = read_one_line(v_client, std::chrono::milliseconds(2500));
        close_sock(v_client);
        v_server.stop();

        ASSERT_FALSE(v_line.empty());
        EXPECT_NE(std::string::npos, v_line.find("\"version\":2"));
        EXPECT_NE(std::string::npos, v_line.find("\"topology\":"));
        EXPECT_NE(std::string::npos, v_line.find("\"workers\":["));
        EXPECT_NE(std::string::npos, v_line.find("\"nodes\":["));
    }

    TEST_F(tcp_server_test, stop_is_safe_when_start_was_not_called) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        EXPECT_NO_THROW(v_server.stop());
    }

    bool send_all(socket_t p_s, const std::string& p_data) {
#if defined(_WIN32)
        return send(p_s, p_data.data(), static_cast<int>(p_data.size()), 0) == static_cast<int>(p_data.size());
#else
        return send(p_s, p_data.data(), p_data.size(), 0) == static_cast<ssize_t>(p_data.size());
#endif
    }

    socket_t connect_loopback(std::uint16_t p_port) {
        const socket_t v_s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (v_s == k_invalid)
            return k_invalid;
        sockaddr_in v_addr{};
        v_addr.sin_family = AF_INET;
        v_addr.sin_port = htons(p_port);
        v_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(v_s, reinterpret_cast<sockaddr*>(&v_addr), sizeof(v_addr)) != 0) {
            close_sock(v_s);
            return k_invalid;
        }
        return v_s;
    }

    bool wait_for(const std::function<bool()>& p_predicate, std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        while (std::chrono::steady_clock::now() < v_deadline) {
            if (p_predicate())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        return p_predicate();
    }

    TEST_F(tcp_server_test, client_set_worker_interval_is_applied) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());

        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":2500}\n"));
        EXPECT_TRUE(wait_for([&] { return v_hub.worker_interval_ms() == 2500u; }, std::chrono::milliseconds(2000)));

        close_sock(v_client);
        v_server.stop();
    }

    TEST_F(tcp_server_test, client_set_node_interval_is_accepted_and_ignored) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());

        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":2500}\n{\"value\":7500,\"kind\":\"set_node_interval_ms\"}\n"));
        EXPECT_TRUE(wait_for([&] { return v_hub.worker_interval_ms() == 2500u; }, std::chrono::milliseconds(2000)));
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        EXPECT_EQ(2500u, v_hub.worker_interval_ms());

        close_sock(v_client);
        v_server.stop();
    }

    TEST_F(tcp_server_test, client_value_is_clamped_to_safe_range) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());

        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":0}\n"));
        EXPECT_TRUE(wait_for([&] { return v_hub.worker_interval_ms() == 50u; }, std::chrono::milliseconds(2000)));

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":99999999}\n"));
        EXPECT_TRUE(wait_for([&] { return v_hub.worker_interval_ms() == 600000u; }, std::chrono::milliseconds(2000)));

        close_sock(v_client);
        v_server.stop();
    }

    TEST_F(tcp_server_test, client_pause_and_resume_are_applied_with_or_without_a_value) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());

        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"pause\"}\n"));
        EXPECT_TRUE(wait_for([&] { return v_hub.paused(); }, std::chrono::milliseconds(2000)));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"resume\",\"value\":0}\n"));
        EXPECT_TRUE(wait_for([&] { return !v_hub.paused(); }, std::chrono::milliseconds(2000)));
        EXPECT_EQ(500u, v_hub.worker_interval_ms());

        close_sock(v_client);
        v_server.stop();
    }

    std::uint16_t free_loopback_port() {
        const socket_t v_s = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in v_addr{};
        v_addr.sin_family = AF_INET;
        v_addr.sin_port = 0;
        v_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(v_s, reinterpret_cast<sockaddr*>(&v_addr), sizeof(v_addr)) != 0) {
            close_sock(v_s);
            return 0;
        }
#if defined(_WIN32)
        int v_len = sizeof(v_addr);
#else
        socklen_t v_len = sizeof(v_addr);
#endif
        ::getsockname(v_s, reinterpret_cast<sockaddr*>(&v_addr), &v_len);
        close_sock(v_s);
        return ntohs(v_addr.sin_port);
    }

    void set_recv_timeout(socket_t p_s, const int p_ms) {
#if defined(_WIN32)
        const auto v_ms = static_cast<DWORD>(p_ms);
        setsockopt(p_s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v_ms), sizeof(v_ms));
#else
        timeval v_tv{};
        v_tv.tv_sec = 0;
        v_tv.tv_usec = p_ms * 1000;
        setsockopt(p_s, SOL_SOCKET, SO_RCVTIMEO, &v_tv, sizeof(v_tv));
#endif
    }

    std::string read_until_closed(socket_t p_s, std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        std::string v_buf;
        char v_chunk[4096];
        while (std::chrono::steady_clock::now() < v_deadline) {
#if defined(_WIN32)
            const int v_n = recv(p_s, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
#else
            const ssize_t v_n = recv(p_s, v_chunk, sizeof(v_chunk), 0);
#endif
            if (v_n == 0) {
                break;
            }
            if (v_n > 0) {
                v_buf.append(v_chunk, v_chunk + v_n);
            }
        }
        return v_buf;
    }

    std::string last_complete_line(const std::string& p_buf) {
        const auto v_end = p_buf.rfind('\n');
        if (v_end == std::string::npos) {
            return {};
        }
        const std::string v_complete = p_buf.substr(0, v_end);
        const auto v_previous = v_complete.rfind('\n');
        return v_previous == std::string::npos ? v_complete : v_complete.substr(v_previous + 1);
    }

    TEST_F(tcp_server_test, shutdown_broadcasts_the_final_frame_before_closing) {
        const std::uint16_t v_port = free_loopback_port();
        gempba::telemetry::configure_port(v_port);

        gempba::telemetry::telemetry_hub v_hub;
        v_hub.set_worker_interval_ms(0);
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/true);

        const socket_t v_client = connect_loopback(v_port);
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 200);

        std::string v_first;
        const auto v_first_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (v_first.find("\"tasks_started_total\":0") == std::string::npos && std::chrono::steady_clock::now() < v_first_deadline) {
            v_first = read_one_line(v_client, std::chrono::milliseconds(500));
        }
        ASSERT_NE(std::string::npos, v_first.find("\"tasks_started_total\":0"));

        v_hub.set_worker_interval_ms(600'000);
        // A tick that read the old interval must finish before the counters change, or it could publish them early.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (int v_i = 0; v_i < 5; ++v_i) {
            v_hub.record_task_local();
        }
        v_hub.shutdown();

        const std::string v_rest = read_until_closed(v_client, std::chrono::milliseconds(3000));
        close_sock(v_client);
        gempba::telemetry::configure_port(9000);

        const std::string v_last = last_complete_line(v_rest);
        EXPECT_NE(std::string::npos, v_last.find("\"tasks_started_total\":5"));
        const std::string v_run_id_key = R"("run_id":")" + v_hub.run_id() + '"';
        EXPECT_FALSE(v_hub.run_id().empty());
        EXPECT_NE(std::string::npos, v_first.find(v_run_id_key)) << v_first;
        EXPECT_NE(std::string::npos, v_last.find(v_run_id_key)) << v_last;
    }

    TEST_F(tcp_server_test, a_paused_center_broadcasts_only_the_envelope_until_resumed) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(50);
        v_hub.tick_if_due();
        ASSERT_TRUE(v_hub.latest_worker_frame(0).has_value());
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);

        const std::string v_paused = read_one_line(v_client, std::chrono::milliseconds(2000));
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::RESUME, 0);
        std::string v_resumed;
        const auto v_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (v_resumed.find("\"paused\":false") == std::string::npos && std::chrono::steady_clock::now() < v_deadline) {
            v_resumed = read_one_line(v_client, std::chrono::milliseconds(500));
        }
        close_sock(v_client);
        v_server.stop();

        EXPECT_TRUE(v_paused.starts_with(R"({"version":2,"run_id":")" + v_hub.run_id() + R"(","ts":)")) << v_paused;
        EXPECT_TRUE(v_paused.ends_with(R"("paused":true})")) << v_paused;
        EXPECT_EQ(std::string::npos, v_paused.find("\"workers\"")) << v_paused;
        EXPECT_NE(std::string::npos, v_resumed.find("\"paused\":false")) << v_resumed;
        EXPECT_NE(std::string::npos, v_resumed.find("\"workers\":[{\"worker_id\":0")) << v_resumed;
    }

    TEST_F(tcp_server_test, shutdown_while_paused_still_broadcasts_the_final_frame) {
        const std::uint16_t v_port = free_loopback_port();
        gempba::telemetry::configure_port(v_port);

        gempba::telemetry::telemetry_hub v_hub;
        v_hub.set_worker_interval_ms(0);
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/true);
        v_hub.apply_control_from_client(gempba::telemetry::control_kind::PAUSE, 0);

        const socket_t v_client = connect_loopback(v_port);
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 200);
        ASSERT_TRUE(read_one_line(v_client, std::chrono::milliseconds(2000)).ends_with(R"("paused":true})"));

        for (int v_i = 0; v_i < 5; ++v_i) {
            v_hub.record_task_local();
        }
        v_hub.shutdown();

        const std::string v_rest = read_until_closed(v_client, std::chrono::milliseconds(3000));
        close_sock(v_client);
        gempba::telemetry::configure_port(9000);

        const std::string v_last = last_complete_line(v_rest);
        EXPECT_NE(std::string::npos, v_last.find("\"paused\":true")) << v_last;
        EXPECT_NE(std::string::npos, v_last.find("\"tasks_started_total\":5")) << v_last;
    }

    TEST_F(tcp_server_test, client_markers_are_unescaped_recorded_from_the_dashboard_and_broadcast) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(50);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);

        ASSERT_TRUE(send_all(v_client, R"({"kind":"mark","label":"phase \"2\" \u00e9 \ud83d\ude80 \/\\"})"
                                       "\n"));
        ASSERT_TRUE(wait_for([&] { return v_hub.run_markers().size() == 1; }, std::chrono::milliseconds(2000)));
        std::string v_line;
        const auto v_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (v_line.find(R"("source":"dashboard")") == std::string::npos && std::chrono::steady_clock::now() < v_deadline) {
            v_line = read_one_line(v_client, std::chrono::milliseconds(500));
        }
        close_sock(v_client);
        v_server.stop();

        const auto v_marker = v_hub.run_markers().front();
        EXPECT_EQ("phase \"2\" \xC3\xA9 \xF0\x9F\x9A\x80 /\\", v_marker.m_label);
        EXPECT_EQ(gempba::telemetry::marker_source::DASHBOARD, v_marker.m_source);
        EXPECT_NE(std::string::npos, v_line.find(R"(,"label":"phase \"2\" )"
                                                 "\xC3\xA9 \xF0\x9F\x9A\x80"
                                                 R"( /\\","source":"dashboard"}])"))
                << v_line;
    }

    TEST_F(tcp_server_test, client_marker_labels_decode_every_json_escape) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"\\b\\f\\n\\r\\t \\u0041 \\u20ac\"}\n"));
        EXPECT_TRUE(wait_for([&] { return !v_hub.run_markers().empty(); }, std::chrono::milliseconds(2000)));
        close_sock(v_client);
        v_server.stop();

        const auto v_markers = v_hub.run_markers();
        ASSERT_EQ(1u, v_markers.size());
        EXPECT_EQ("\b\f\n\r\t A \xE2\x82\xAC", v_markers[0].m_label);
    }

    TEST_F(tcp_server_test, malformed_or_over_limit_markers_are_dropped) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":99999999999}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":\"fast\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"bad \\x escape\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"lone \\ud83d surrogate\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"bad \\uZZZZ hex\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"cut \\u12\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"unterminated}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"" + std::string(gempba::telemetry::MAX_RUN_MARKER_LABEL_BYTES + 1, 'x') + "\"}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"mark\",\"label\":\"ok\"}\n"));

        EXPECT_TRUE(wait_for([&] { return !v_hub.run_markers().empty(); }, std::chrono::milliseconds(2000)));
        close_sock(v_client);
        v_server.stop();

        const auto v_markers = v_hub.run_markers();
        ASSERT_EQ(1u, v_markers.size());
        EXPECT_EQ("ok", v_markers[0].m_label);
        EXPECT_EQ(500u, v_hub.worker_interval_ms());
    }

    std::vector<std::chrono::steady_clock::time_point> line_arrivals(socket_t p_s, const std::size_t p_count, std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        std::vector<std::chrono::steady_clock::time_point> v_arrivals;
        char v_chunk[4096];
        while (v_arrivals.size() < p_count && std::chrono::steady_clock::now() < v_deadline) {
#if defined(_WIN32)
            const int v_n = recv(p_s, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
#else
            const ssize_t v_n = recv(p_s, v_chunk, sizeof(v_chunk), 0);
#endif
            const auto v_now = std::chrono::steady_clock::now();
            for (int v_i = 0; v_i < static_cast<int>(v_n); ++v_i) {
                if (v_chunk[v_i] == '\n') {
                    v_arrivals.push_back(v_now);
                }
            }
        }
        return v_arrivals;
    }

    TEST_F(tcp_server_test, broadcast_follows_a_short_worker_interval) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(100);
        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);

        // Three lines at the old fixed 500 ms cadence take at least 1.5 s.
        const auto v_arrivals = line_arrivals(v_client, 3, std::chrono::milliseconds(1400));
        close_sock(v_client);
        v_server.stop();

        EXPECT_EQ(3u, v_arrivals.size());
    }

    TEST_F(tcp_server_test, broadcast_waits_for_a_long_worker_interval) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(1000);
        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);

        const auto v_arrivals = line_arrivals(v_client, 2, std::chrono::milliseconds(3500));
        close_sock(v_client);
        v_server.stop();

        ASSERT_EQ(2u, v_arrivals.size());
        EXPECT_GE(v_arrivals[1] - v_arrivals[0], std::chrono::milliseconds(700));
    }

    TEST_F(tcp_server_test, client_rate_change_applies_to_the_broadcast_without_a_restart) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(5000);
        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);

        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":100}\n"));
        const auto v_arrivals = line_arrivals(v_client, 3, std::chrono::milliseconds(1400));
        close_sock(v_client);
        v_server.stop();

        EXPECT_EQ(100u, v_hub.worker_interval_ms());
        EXPECT_EQ(3u, v_arrivals.size());
    }

    TEST_F(tcp_server_test, stop_is_prompt_and_sends_the_final_line_with_a_long_worker_interval) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(50);
        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());
        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);
        set_recv_timeout(v_client, 100);
        ASSERT_EQ(1u, line_arrivals(v_client, 1, std::chrono::milliseconds(2000)).size());
        v_hub.set_worker_interval_ms(600'000);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        const auto v_start = std::chrono::steady_clock::now();
        v_server.stop();
        const auto v_stop_took = std::chrono::steady_clock::now() - v_start;

        set_recv_timeout(v_client, 200);
        const std::string v_rest = read_until_closed(v_client, std::chrono::milliseconds(2000));
        close_sock(v_client);

        EXPECT_LT(v_stop_took, std::chrono::milliseconds(1000));
        EXPECT_NE(std::string::npos, v_rest.find("\"version\":2")) << v_rest;
    }

    TEST_F(tcp_server_test, malformed_lines_are_ignored_and_do_not_break_subsequent_input) {
        gempba::telemetry::telemetry_hub v_hub;
        v_hub.on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1, /*p_start_pump_thread=*/false);
        v_hub.set_worker_interval_ms(500);

        gempba::telemetry::center_tcp_server v_server(0, v_hub);
        ASSERT_TRUE(v_server.start());

        const socket_t v_client = connect_loopback(v_server.bound_port());
        ASSERT_NE(k_invalid, v_client);

        ASSERT_TRUE(send_all(v_client, "this is not json\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"unknown_kind\",\"value\":1000}\n"));
        ASSERT_TRUE(send_all(v_client, "{\"kind\":\"set_worker_interval_ms\",\"value\":1234}\n"));

        EXPECT_TRUE(wait_for([&] { return v_hub.worker_interval_ms() == 1234u; }, std::chrono::milliseconds(2000)));

        close_sock(v_client);
        v_server.stop();
    }

} // namespace
