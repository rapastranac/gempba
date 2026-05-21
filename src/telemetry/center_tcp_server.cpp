#include <impl/telemetry/center_tcp_server.hpp>

#include <gempba/telemetry/telemetry_hub.hpp>
#include <impl/telemetry/json_serializer.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
    #include <winsock2.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

namespace gempba::telemetry {

    namespace {

#if defined(_WIN32)
        using socket_t = SOCKET;
        constexpr socket_t k_invalid_socket = INVALID_SOCKET;

        struct winsock_init {
            winsock_init() noexcept {
                WSADATA v_data;
                m_ok = (WSAStartup(MAKEWORD(2, 2), &v_data) == 0);
            }
            ~winsock_init() noexcept {
                if (m_ok)
                    WSACleanup();
            }
            bool m_ok = false;
        };

        // One Winsock init per process, refcounted by static lifetime.
        winsock_init& winsock_keep_alive() noexcept {
            static winsock_init v_init;
            return v_init;
        }

        void close_socket(socket_t p_s) noexcept { closesocket(p_s); }
        // Forces a blocking accept/recv on this socket to return promptly.
        void shutdown_socket(socket_t p_s) noexcept { ::shutdown(p_s, SD_BOTH); }
        bool send_all(socket_t p_s, const char* p_buf, std::size_t p_len) noexcept {
            std::size_t v_written = 0;
            while (v_written < p_len) {
                const int v_n = ::send(p_s, p_buf + v_written, static_cast<int>(p_len - v_written), 0);
                if (v_n == SOCKET_ERROR)
                    return false;
                v_written += static_cast<std::size_t>(v_n);
            }
            return true;
        }
#else
        using socket_t = int;
        constexpr socket_t k_invalid_socket = -1;

        // No-op on POSIX — kept here so the call site below is symmetric.
        struct posix_keep_alive {};
        posix_keep_alive& winsock_keep_alive() noexcept {
            static posix_keep_alive v_init;
            return v_init;
        }

        void close_socket(socket_t p_s) noexcept { ::close(p_s); }
        // close() alone does not reliably unblock a sibling thread blocked in
        // accept()/recv() on Linux; shutdown(SHUT_RDWR) does.
        void shutdown_socket(socket_t p_s) noexcept { ::shutdown(p_s, SHUT_RDWR); }
        bool send_all(socket_t p_s, const char* p_buf, std::size_t p_len) noexcept {
            std::size_t v_written = 0;
            while (v_written < p_len) {
                const ssize_t v_n = ::send(p_s, p_buf + v_written, p_len - v_written, MSG_NOSIGNAL);
                if (v_n <= 0)
                    return false;
                v_written += static_cast<std::size_t>(v_n);
            }
            return true;
        }
#endif

        constexpr std::chrono::milliseconds k_broadcast_interval{500};

        constexpr std::size_t k_inbox_cap = 4096;

        std::string current_hostname() {
            char v_buf[64] = {};
            if (::gethostname(v_buf, sizeof(v_buf) - 1) == 0) {
                return v_buf;
            }
            return "unknown";
        }

        std::string_view trim(std::string_view p_s) noexcept {
            while (!p_s.empty() && std::isspace(static_cast<unsigned char>(p_s.front())))
                p_s.remove_prefix(1);
            while (!p_s.empty() && std::isspace(static_cast<unsigned char>(p_s.back())))
                p_s.remove_suffix(1);
            return p_s;
        }

        bool parse_control_line(std::string_view p_line, control_kind& p_out_kind, std::uint32_t& p_out_value) noexcept {
            const auto v_find_string_field = [&](std::string_view p_name) -> std::optional<std::string_view> {
                std::string v_pat;
                v_pat.reserve(p_name.size() + 4);
                v_pat += '"';
                v_pat.append(p_name);
                v_pat += "\":";
                const auto v_pos = p_line.find(v_pat);
                if (v_pos == std::string_view::npos)
                    return std::nullopt;
                std::size_t v_i = v_pos + v_pat.size();
                while (v_i < p_line.size() && std::isspace(static_cast<unsigned char>(p_line[v_i])))
                    ++v_i;
                if (v_i >= p_line.size() || p_line[v_i] != '"')
                    return std::nullopt;
                ++v_i;
                const std::size_t v_start = v_i;
                while (v_i < p_line.size() && p_line[v_i] != '"')
                    ++v_i;
                if (v_i >= p_line.size())
                    return std::nullopt;
                return p_line.substr(v_start, v_i - v_start);
            };

            const auto v_find_uint_field = [&](std::string_view p_name) -> std::optional<std::uint32_t> {
                std::string v_pat;
                v_pat.reserve(p_name.size() + 4);
                v_pat += '"';
                v_pat.append(p_name);
                v_pat += "\":";
                const auto v_pos = p_line.find(v_pat);
                if (v_pos == std::string_view::npos)
                    return std::nullopt;
                std::size_t v_i = v_pos + v_pat.size();
                while (v_i < p_line.size() && std::isspace(static_cast<unsigned char>(p_line[v_i])))
                    ++v_i;
                std::uint64_t v_acc = 0;
                bool v_any = false;
                while (v_i < p_line.size() && std::isdigit(static_cast<unsigned char>(p_line[v_i]))) {
                    v_acc = v_acc * 10 + static_cast<std::uint64_t>(p_line[v_i] - '0');
                    if (v_acc > std::numeric_limits<std::uint32_t>::max())
                        return std::nullopt;
                    ++v_i;
                    v_any = true;
                }
                if (!v_any)
                    return std::nullopt;
                return static_cast<std::uint32_t>(v_acc);
            };

            const auto v_kind_str = v_find_string_field("kind");
            const auto v_value = v_find_uint_field("value");
            if (!v_kind_str || !v_value)
                return false;
            const auto v_kind_trimmed = trim(*v_kind_str);
            if (v_kind_trimmed == "set_worker_interval_ms") {
                p_out_kind = control_kind::SET_WORKER_INTERVAL_MS;
            } else if (v_kind_trimmed == "set_node_interval_ms") {
                p_out_kind = control_kind::SET_NODE_INTERVAL_MS;
            } else {
                return false;
            }
            p_out_value = *v_value;
            return true;
        }

    } // namespace

    struct client_state {
        socket_t m_sock = k_invalid_socket;
        std::string m_inbox;
    };

    struct center_tcp_server::impl {
        std::uint16_t m_requested_port = 0;
        std::uint16_t m_bound_port = 0;
        telemetry_hub* m_hub = nullptr;

        socket_t m_listen_sock = k_invalid_socket;
        std::atomic<bool> m_running{false};
        std::thread m_accept_thread;
        std::thread m_broadcast_thread;

        std::mutex m_clients_mtx;
        std::vector<client_state> m_clients;
    };

    center_tcp_server::center_tcp_server(const std::uint16_t p_port, telemetry_hub& p_hub) noexcept : m_impl(std::make_unique<impl>()) {
        m_impl->m_requested_port = p_port;
        m_impl->m_hub = &p_hub;
    }

    center_tcp_server::~center_tcp_server() { stop(); }

    bool center_tcp_server::start() {
        (void) winsock_keep_alive();

        m_impl->m_listen_sock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_impl->m_listen_sock == k_invalid_socket)
            return false;

        int v_yes = 1;
        ::setsockopt(m_impl->m_listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&v_yes), sizeof(v_yes));

        sockaddr_in v_addr{};
        v_addr.sin_family = AF_INET;
        v_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        v_addr.sin_port = htons(m_impl->m_requested_port);

        if (::bind(m_impl->m_listen_sock, reinterpret_cast<sockaddr*>(&v_addr), sizeof(v_addr)) != 0) {
            close_socket(m_impl->m_listen_sock);
            m_impl->m_listen_sock = k_invalid_socket;
            return false;
        }
        if (::listen(m_impl->m_listen_sock, 8) != 0) {
            close_socket(m_impl->m_listen_sock);
            m_impl->m_listen_sock = k_invalid_socket;
            return false;
        }

        sockaddr_in v_bound{};
#if defined(_WIN32)
        int v_blen = sizeof(v_bound);
#else
        socklen_t v_blen = sizeof(v_bound);
#endif
        if (::getsockname(m_impl->m_listen_sock, reinterpret_cast<sockaddr*>(&v_bound), &v_blen) == 0) {
            m_impl->m_bound_port = ntohs(v_bound.sin_port);
        }

        spdlog::info("telemetry listening on 127.0.0.1:{} on host {}", m_impl->m_bound_port, current_hostname());

        m_impl->m_running.store(true, std::memory_order_release);

        m_impl->m_accept_thread = std::thread([v_impl_ptr = m_impl.get()] {
            while (v_impl_ptr->m_running.load(std::memory_order_acquire)) {
                sockaddr_in v_peer{};
#if defined(_WIN32)
                int v_plen = sizeof(v_peer);
#else
                socklen_t v_plen = sizeof(v_peer);
#endif
                const socket_t v_client = ::accept(v_impl_ptr->m_listen_sock, reinterpret_cast<sockaddr*>(&v_peer), &v_plen);
                if (v_client == k_invalid_socket) {
                    // listen socket likely closed during stop() → exit
                    break;
                }
                {
                    const std::scoped_lock v_lock(v_impl_ptr->m_clients_mtx);
                    v_impl_ptr->m_clients.push_back(client_state{v_client, {}});
                }
            }
        });

        m_impl->m_broadcast_thread = std::thread([v_impl_ptr = m_impl.get()] {
            const auto v_drain_inbound = [&](client_state& p_cs) -> bool {
                while (true) {
                    fd_set v_rfds;
                    FD_ZERO(&v_rfds);
                    FD_SET(p_cs.m_sock, &v_rfds);
                    timeval v_tv{0, 0};
                    const int v_ready = ::select(static_cast<int>(p_cs.m_sock) + 1, &v_rfds, nullptr, nullptr, &v_tv);
                    if (v_ready <= 0)
                        break;

                    char v_buf[256];
#if defined(_WIN32)
                    const int v_n = ::recv(p_cs.m_sock, v_buf, static_cast<int>(sizeof(v_buf)), 0);
                    if (v_n == SOCKET_ERROR)
                        return false;
#else
                    const ssize_t v_n = ::recv(p_cs.m_sock, v_buf, sizeof(v_buf), 0);
                    if (v_n < 0)
                        return false;
#endif
                    if (v_n == 0)
                        return false;
                    p_cs.m_inbox.append(v_buf, static_cast<std::size_t>(v_n));
                    if (p_cs.m_inbox.size() > k_inbox_cap)
                        return false;
                }

                while (true) {
                    const auto v_nl = p_cs.m_inbox.find('\n');
                    if (v_nl == std::string::npos)
                        break;
                    const std::string_view v_line(p_cs.m_inbox.data(), v_nl);
                    control_kind v_kind{};
                    std::uint32_t v_value = 0;
                    if (parse_control_line(v_line, v_kind, v_value)) {
                        if (v_impl_ptr->m_hub != nullptr)
                            v_impl_ptr->m_hub->apply_control_from_client(v_kind, v_value);
                    } else {
                        spdlog::warn("telemetry: ignoring malformed control line ({} bytes)", v_nl);
                    }
                    p_cs.m_inbox.erase(0, v_nl + 1);
                }
                return true;
            };

            while (v_impl_ptr->m_running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(k_broadcast_interval);
                if (!v_impl_ptr->m_running.load(std::memory_order_acquire))
                    break;

                // Build the broadcast payload from the hub's current state.
                const auto& v_topology = v_impl_ptr->m_hub->current_topology();
                std::vector<worker_frame> v_workers;
                std::vector<node_frame> v_nodes;
                for (const auto& v_node: v_topology.m_nodes) {
                    for (std::uint32_t v_id: v_node.m_worker_ids) {
                        if (auto v_f = v_impl_ptr->m_hub->latest_worker_frame(v_id)) {
                            v_workers.push_back(*v_f);
                        }
                    }
                    if (auto v_n = v_impl_ptr->m_hub->latest_node_frame(v_node.m_sentinel_worker_id)) {
                        v_nodes.push_back(*v_n);
                    }
                }

                const auto v_now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

                broadcast_payload v_payload{};
                v_payload.m_now_ms = static_cast<std::uint64_t>(v_now);
                v_payload.m_elapsed_seconds = v_impl_ptr->m_hub->program_elapsed_seconds();
                v_payload.m_topology = &v_topology;
                v_payload.m_workers = &v_workers;
                v_payload.m_nodes = &v_nodes;

                std::string v_line;
                v_line.reserve(2048);
                serialize_broadcast(v_line, v_payload);
                v_line += '\n';

                std::vector<socket_t> v_dead;
                {
                    const std::scoped_lock v_lock(v_impl_ptr->m_clients_mtx);
                    for (auto& v_cs: v_impl_ptr->m_clients) {
                        if (!v_drain_inbound(v_cs)) {
                            v_dead.push_back(v_cs.m_sock);
                            continue;
                        }
                        if (!send_all(v_cs.m_sock, v_line.data(), v_line.size())) {
                            v_dead.push_back(v_cs.m_sock);
                        }
                    }
                    if (!v_dead.empty()) {
                        v_impl_ptr->m_clients.erase(std::remove_if(v_impl_ptr->m_clients.begin(), v_impl_ptr->m_clients.end(),
                                                                   [&](const client_state& p_cs) { return std::find(v_dead.begin(), v_dead.end(), p_cs.m_sock) != v_dead.end(); }),
                                                    v_impl_ptr->m_clients.end());
                    }
                }
                for (socket_t v_c: v_dead)
                    close_socket(v_c);
            }
        });

        return true;
    }

    void center_tcp_server::stop() {
        if (!m_impl)
            return;
        if (!m_impl->m_running.exchange(false, std::memory_order_acq_rel)) {
            // start() never succeeded or stop() already ran
            if (m_impl->m_listen_sock != k_invalid_socket) {
                shutdown_socket(m_impl->m_listen_sock);
                close_socket(m_impl->m_listen_sock);
                m_impl->m_listen_sock = k_invalid_socket;
            }
            return;
        }

        if (m_impl->m_listen_sock != k_invalid_socket) {
            // shutdown() before close() so the accept thread, blocked in
            // accept(), unblocks reliably on Linux/macOS as well as Windows.
            shutdown_socket(m_impl->m_listen_sock);
            close_socket(m_impl->m_listen_sock);
            m_impl->m_listen_sock = k_invalid_socket;
        }

        if (m_impl->m_accept_thread.joinable())
            m_impl->m_accept_thread.join();
        if (m_impl->m_broadcast_thread.joinable())
            m_impl->m_broadcast_thread.join();

        const std::scoped_lock v_lock(m_impl->m_clients_mtx);
        for (const auto& v_cs: m_impl->m_clients)
            close_socket(v_cs.m_sock);
        m_impl->m_clients.clear();
    }

    std::uint16_t center_tcp_server::bound_port() const noexcept { return m_impl ? m_impl->m_bound_port : 0; }

} // namespace gempba::telemetry
