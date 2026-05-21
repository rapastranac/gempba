#include <chrono>
#include <cstring>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/center_tcp_server.hpp>
#include <string>
#include <thread>

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
        EXPECT_NE(std::string::npos, v_line.find("\"version\":1"));
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

} // namespace
