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
#include <chrono>
#include <cstdint>
#include <gempba/gempba.hpp>
#include <gtest/gtest.h>
#include <string>
#include <thread>

#if defined(_WIN32)
    #include <winsock2.h>
using socket_t = SOCKET;
static constexpr socket_t k_invalid = INVALID_SOCKET;
static void close_sock(const socket_t p_s) { closesocket(p_s); }
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
using socket_t = int;
static constexpr socket_t k_invalid = -1;
static void close_sock(const socket_t p_s) { ::close(p_s); }
#endif

namespace {

    constexpr std::uint16_t k_default_port = 9000;

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
            close_sock(v_socket);
            return 0;
        }
#if defined(_WIN32)
        int v_length = sizeof(v_address);
#else
        socklen_t v_length = sizeof(v_address);
#endif
        ::getsockname(v_socket, reinterpret_cast<sockaddr*>(&v_address), &v_length);
        close_sock(v_socket);
        return ntohs(v_address.sin_port);
    }

    socket_t connect_loopback(const std::uint16_t p_port, const std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        while (std::chrono::steady_clock::now() < v_deadline) {
            const socket_t v_socket = ::socket(AF_INET, SOCK_STREAM, 0);
            const sockaddr_in v_address = loopback_address(p_port);
            if (::connect(v_socket, reinterpret_cast<const sockaddr*>(&v_address), sizeof(v_address)) == 0) {
#if defined(_WIN32)
                const DWORD v_timeout_ms = 200;
                setsockopt(v_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&v_timeout_ms), sizeof(v_timeout_ms));
#else
                timeval v_timeout{0, 200'000};
                setsockopt(v_socket, SOL_SOCKET, SO_RCVTIMEO, &v_timeout, sizeof(v_timeout));
#endif
                return v_socket;
            }
            close_sock(v_socket);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return k_invalid;
    }

    std::string read_one_line(const socket_t p_socket, const std::chrono::milliseconds p_timeout) {
        const auto v_deadline = std::chrono::steady_clock::now() + p_timeout;
        std::string v_buffer;
        char v_chunk[4096];
        while (std::chrono::steady_clock::now() < v_deadline) {
            const auto v_count = ::recv(p_socket, v_chunk, static_cast<int>(sizeof(v_chunk)), 0);
            if (v_count > 0) {
                v_buffer.append(v_chunk, v_chunk + v_count);
                const auto v_end = v_buffer.find('\n');
                if (v_end != std::string::npos) {
                    return v_buffer.substr(0, v_end);
                }
            }
        }
        return {};
    }

    class gempba_telemetry_port_test : public ::testing::Test {
    protected:
#if defined(_WIN32)
        WSADATA m_wsa{};
        void SetUp() override { WSAStartup(MAKEWORD(2, 2), &m_wsa); }
#endif

        void TearDown() override {
            gempba::shutdown();
            gempba::telemetry::configure_port(k_default_port);
#if defined(_WIN32)
            WSACleanup();
#endif
        }
    };

    TEST_F(gempba_telemetry_port_test, node_manager_serves_telemetry_on_the_configured_port) {
        const std::uint16_t v_port = free_loopback_port();
        ASSERT_NE(0, v_port);
        ASSERT_NE(k_default_port, v_port);

        gempba::telemetry::configure_port(v_port);
        gempba::create_node_manager(gempba::create_load_balancer(gempba::QUASI_HORIZONTAL));

        const socket_t v_client = connect_loopback(v_port, std::chrono::milliseconds(3000));
        ASSERT_NE(k_invalid, v_client);
        const std::string v_line = read_one_line(v_client, std::chrono::milliseconds(3000));
        close_sock(v_client);

        EXPECT_NE(std::string::npos, v_line.find(R"("topology":)")) << v_line;
    }

} // namespace
