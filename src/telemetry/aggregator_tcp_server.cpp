#include <impl/telemetry/aggregator_tcp_server.hpp>

#include <gempba/telemetry/telemetry_hub.hpp>
#include <impl/telemetry/json_serializer.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
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

        constexpr std::chrono::milliseconds k_min_broadcast_interval{50};
        constexpr std::chrono::milliseconds k_control_poll_interval{500};
        constexpr std::chrono::milliseconds k_snapshot_poll_interval{10};

        std::chrono::milliseconds broadcast_interval(const telemetry_hub& p_hub) { return std::max(std::chrono::milliseconds(p_hub.interval_ms()), k_min_broadcast_interval); }

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

        std::optional<std::size_t> find_field_value(const std::string_view p_line, const std::string_view p_name) {
            std::string v_pat;
            v_pat.reserve(p_name.size() + 4);
            v_pat += '"';
            v_pat.append(p_name);
            v_pat += "\":";
            const auto v_pos = p_line.find(v_pat);
            if (v_pos == std::string_view::npos) {
                return std::nullopt;
            }
            std::size_t v_i = v_pos + v_pat.size();
            while (v_i < p_line.size() && std::isspace(static_cast<unsigned char>(p_line[v_i]))) {
                ++v_i;
            }
            return v_i;
        }

        std::optional<std::uint32_t> parse_hex4(const std::string_view p_line, const std::size_t p_at) {
            if (p_at + 4 > p_line.size()) {
                return std::nullopt;
            }
            std::uint32_t v_value = 0;
            const char* v_end = p_line.data() + p_at + 4;
            const auto [v_ptr, v_ec] = std::from_chars(p_line.data() + p_at, v_end, v_value, 16);
            if (v_ec != std::errc{} || v_ptr != v_end) {
                return std::nullopt;
            }
            return v_value;
        }

        void append_utf8(std::string& p_out, const std::uint32_t p_code_point) {
            if (p_code_point < 0x80) {
                p_out += static_cast<char>(p_code_point);
            } else if (p_code_point < 0x800) {
                p_out += static_cast<char>(0xC0 | (p_code_point >> 6));
                p_out += static_cast<char>(0x80 | (p_code_point & 0x3F));
            } else if (p_code_point < 0x10000) {
                p_out += static_cast<char>(0xE0 | (p_code_point >> 12));
                p_out += static_cast<char>(0x80 | ((p_code_point >> 6) & 0x3F));
                p_out += static_cast<char>(0x80 | (p_code_point & 0x3F));
            } else {
                p_out += static_cast<char>(0xF0 | (p_code_point >> 18));
                p_out += static_cast<char>(0x80 | ((p_code_point >> 12) & 0x3F));
                p_out += static_cast<char>(0x80 | ((p_code_point >> 6) & 0x3F));
                p_out += static_cast<char>(0x80 | (p_code_point & 0x3F));
            }
        }

        std::optional<std::string> find_string_field(const std::string_view p_line, const std::string_view p_name) {
            const auto v_at = find_field_value(p_line, p_name);
            if (!v_at || *v_at >= p_line.size() || p_line[*v_at] != '"') {
                return std::nullopt;
            }
            std::string v_out;
            for (std::size_t v_i = *v_at + 1; v_i < p_line.size(); ++v_i) {
                const char v_c = p_line[v_i];
                if (v_c == '"') {
                    return v_out;
                }
                if (v_c != '\\') {
                    v_out += v_c;
                    continue;
                }
                if (++v_i >= p_line.size()) {
                    return std::nullopt;
                }
                switch (p_line[v_i]) {
                    case '"':
                    case '\\':
                    case '/':
                        v_out += p_line[v_i];
                        break;
                    case 'b':
                        v_out += '\b';
                        break;
                    case 'f':
                        v_out += '\f';
                        break;
                    case 'n':
                        v_out += '\n';
                        break;
                    case 'r':
                        v_out += '\r';
                        break;
                    case 't':
                        v_out += '\t';
                        break;
                    case 'u': {
                        const auto v_unit = parse_hex4(p_line, v_i + 1);
                        if (!v_unit || (*v_unit >= 0xDC00 && *v_unit <= 0xDFFF)) {
                            return std::nullopt;
                        }
                        v_i += 4;
                        std::uint32_t v_code_point = *v_unit;
                        if (*v_unit >= 0xD800 && *v_unit <= 0xDBFF) {
                            const auto v_low = v_i + 2 < p_line.size() && p_line[v_i + 1] == '\\' && p_line[v_i + 2] == 'u' ? parse_hex4(p_line, v_i + 3) : std::nullopt;
                            if (!v_low || *v_low < 0xDC00 || *v_low > 0xDFFF) {
                                return std::nullopt;
                            }
                            v_i += 6;
                            v_code_point = 0x10000 + ((*v_unit - 0xD800) << 10) + (*v_low - 0xDC00);
                        }
                        append_utf8(v_out, v_code_point);
                        break;
                    }
                    default:
                        return std::nullopt;
                }
            }
            return std::nullopt;
        }

        std::optional<std::uint32_t> find_uint_field(const std::string_view p_line, const std::string_view p_name) {
            const auto v_at = find_field_value(p_line, p_name);
            if (!v_at) {
                return std::nullopt;
            }
            std::uint64_t v_acc = 0;
            bool v_any = false;
            for (std::size_t v_i = *v_at; v_i < p_line.size() && std::isdigit(static_cast<unsigned char>(p_line[v_i])); ++v_i) {
                v_acc = v_acc * 10 + static_cast<std::uint64_t>(p_line[v_i] - '0');
                if (v_acc > std::numeric_limits<std::uint32_t>::max()) {
                    return std::nullopt;
                }
                v_any = true;
            }
            if (!v_any) {
                return std::nullopt;
            }
            return static_cast<std::uint32_t>(v_acc);
        }

        std::optional<std::string> parse_marker_line(const std::string_view p_line) {
            const auto v_kind = find_string_field(p_line, "kind");
            if (!v_kind || trim(*v_kind) != "mark") {
                return std::nullopt;
            }
            return find_string_field(p_line, "label");
        }

        bool parse_control_line(std::string_view p_line, control_kind& p_out_kind, std::uint32_t& p_out_value) {
            const auto v_kind_str = find_string_field(p_line, "kind");
            if (!v_kind_str) {
                return false;
            }
            const auto v_kind_trimmed = trim(*v_kind_str);
            for (const auto& [v_name, v_kind]: {std::pair{"pause", control_kind::PAUSE}, std::pair{"resume", control_kind::RESUME}, std::pair{"snapshot", control_kind::SNAPSHOT}}) {
                if (v_kind_trimmed == v_name) {
                    p_out_kind = v_kind;
                    p_out_value = 0;
                    return true;
                }
            }
            const auto v_value = find_uint_field(p_line, "value");
            if (!v_value) {
                return false;
            }
            if (v_kind_trimmed == "set_interval_ms" || v_kind_trimmed == "set_worker_interval_ms") {
                p_out_kind = control_kind::SET_INTERVAL_MS;
            } else if (v_kind_trimmed == "set_node_interval_ms") {
                // Sent by dashboards from before #367 folded the node rate into the worker rate; accepted and ignored for v4.3.0, to be removed in the next breaking release.
                p_out_kind = control_kind::UNSPECIFIED;
            } else {
                return false;
            }
            p_out_value = *v_value;
            return true;
        }

        std::string build_broadcast_line(telemetry_hub& p_hub, const bool p_with_frames, const bool p_final) {
            const auto v_now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

            broadcast_payload v_payload{};
            v_payload.m_run_id = p_hub.run_id();
            v_payload.m_now_ms = static_cast<std::uint64_t>(v_now);
            v_payload.m_elapsed_seconds = p_hub.program_elapsed_seconds();
            v_payload.m_paused = p_hub.paused();
            v_payload.m_final = p_final;

            std::string v_line;
            if (!p_with_frames) {
                serialize_paused_broadcast(v_line, v_payload);
                v_line += '\n';
                return v_line;
            }

            const auto& v_topology = p_hub.current_topology();
            std::vector<worker_frame> v_workers;
            std::vector<node_frame> v_nodes;
            for (const auto& v_node: v_topology.m_nodes) {
                for (std::uint32_t v_id: v_node.m_worker_ids) {
                    if (auto v_f = p_hub.latest_rank_frame(v_id)) {
                        v_workers.push_back(*v_f);
                    }
                }
                if (auto v_n = p_hub.latest_node_frame(v_node.m_sentinel_worker_id)) {
                    v_nodes.push_back(*v_n);
                }
            }

            const run_description v_run = p_hub.describe_run();
            v_payload.m_topology = &v_topology;
            v_payload.m_run = &v_run;
            v_payload.m_workers = &v_workers;
            v_payload.m_nodes = &v_nodes;

            v_line.reserve(2048);
            serialize_broadcast(v_line, v_payload);
            v_line += '\n';
            return v_line;
        }

    } // namespace

    struct client_state {
        socket_t m_sock = k_invalid_socket;
        std::string m_inbox;
    };

    struct aggregator_tcp_server::impl {
        std::uint16_t m_requested_port = 0;
        std::uint16_t m_bound_port = 0;
        telemetry_hub* m_hub = nullptr;

        socket_t m_listen_sock = k_invalid_socket;
        std::atomic<bool> m_running{false};
        std::thread m_accept_thread;
        std::thread m_broadcast_thread;
        std::mutex m_wake_mtx;
        std::condition_variable m_wake_cv;

        std::mutex m_clients_mtx;
        std::vector<client_state> m_clients;
    };

    aggregator_tcp_server::aggregator_tcp_server(const std::uint16_t p_port, telemetry_hub& p_hub) noexcept : m_impl(std::make_unique<impl>()) {
        m_impl->m_requested_port = p_port;
        m_impl->m_hub = &p_hub;
    }

    aggregator_tcp_server::~aggregator_tcp_server() { stop(); }

    bool aggregator_tcp_server::start() {
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
                    if (const auto v_label = parse_marker_line(v_line)) {
                        if (v_impl_ptr->m_hub != nullptr) {
                            (void) v_impl_ptr->m_hub->mark_run(*v_label, marker_source::DASHBOARD);
                        }
                    } else if (parse_control_line(v_line, v_kind, v_value)) {
                        if (v_impl_ptr->m_hub != nullptr)
                            v_impl_ptr->m_hub->apply_control_from_client(v_kind, v_value);
                    } else {
                        spdlog::warn("telemetry: ignoring malformed control line ({} bytes)", v_nl);
                    }
                    p_cs.m_inbox.erase(0, v_nl + 1);
                }
                return true;
            };

            const auto v_stopped = [v_impl_ptr] { return !v_impl_ptr->m_running.load(std::memory_order_acquire); };
            auto v_last_broadcast = std::chrono::steady_clock::now();
            std::uint32_t v_broadcast_snapshot = 0;
            while (!v_stopped()) {
                {
                    const auto v_due = v_last_broadcast + broadcast_interval(*v_impl_ptr->m_hub);
                    const auto v_poll = v_impl_ptr->m_hub->snapshot_requested() != v_broadcast_snapshot ? k_snapshot_poll_interval : k_control_poll_interval;
                    std::unique_lock v_wake_lock(v_impl_ptr->m_wake_mtx);
                    v_impl_ptr->m_wake_cv.wait_until(v_wake_lock, std::min(v_due, std::chrono::steady_clock::now() + v_poll), v_stopped);
                }
                if (v_stopped()) {
                    break;
                }

                std::vector<socket_t> v_dead;
                {
                    const std::scoped_lock v_lock(v_impl_ptr->m_clients_mtx);
                    for (auto& v_cs: v_impl_ptr->m_clients) {
                        if (!v_drain_inbound(v_cs)) {
                            v_dead.push_back(v_cs.m_sock);
                        }
                    }
                }

                const std::uint32_t v_snapshot = v_impl_ptr->m_hub->snapshot_requested();
                const bool v_snapshot_ready = v_snapshot != v_broadcast_snapshot && !v_impl_ptr->m_hub->snapshot_outstanding();
                if (v_snapshot_ready || std::chrono::steady_clock::now() >= v_last_broadcast + broadcast_interval(*v_impl_ptr->m_hub)) {
                    v_last_broadcast = std::chrono::steady_clock::now();
                    if (v_snapshot_ready) {
                        v_broadcast_snapshot = v_snapshot;
                    }
                    const std::string v_line = build_broadcast_line(*v_impl_ptr->m_hub, v_snapshot_ready || !v_impl_ptr->m_hub->paused(), false);
                    const std::scoped_lock v_lock(v_impl_ptr->m_clients_mtx);
                    for (const auto& v_cs: v_impl_ptr->m_clients) {
                        const bool v_already_dead = std::find(v_dead.begin(), v_dead.end(), v_cs.m_sock) != v_dead.end();
                        if (!v_already_dead && !send_all(v_cs.m_sock, v_line.data(), v_line.size())) {
                            v_dead.push_back(v_cs.m_sock);
                        }
                    }
                }

                {
                    const std::scoped_lock v_lock(v_impl_ptr->m_clients_mtx);
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

    void aggregator_tcp_server::stop() {
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
        {
            const std::scoped_lock v_lock(m_impl->m_wake_mtx);
        }
        m_impl->m_wake_cv.notify_all();

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
        if (!m_impl->m_clients.empty()) {
            const std::string v_line = build_broadcast_line(*m_impl->m_hub, true, true);
            for (const auto& v_cs: m_impl->m_clients) {
                (void) send_all(v_cs.m_sock, v_line.data(), v_line.size());
            }
        }
        for (const auto& v_cs: m_impl->m_clients)
            close_socket(v_cs.m_sock);
        m_impl->m_clients.clear();
    }

    std::uint16_t aggregator_tcp_server::bound_port() const noexcept { return m_impl ? m_impl->m_bound_port : 0; }

} // namespace gempba::telemetry
