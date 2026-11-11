#ifndef GEMPBA_TELEMETRY_CLIENT_OUTBOX_HPP
#define GEMPBA_TELEMETRY_CLIENT_OUTBOX_HPP

#include <algorithm>
#include <cstddef>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gempba::telemetry {

    /**
     * @brief The broadcast lines waiting to reach one client, oldest first.
     *
     * Holds at most @c capacity lines that haven't started going out. Pushing past that drops the oldest of them, so a client that
     * reads slower than the stream skips whole lines and always keeps the newest. A line already partly written is never dropped,
     * so the client never receives half a line. Not thread-safe.
     */
    class client_outbox {
    public:
        using line = std::shared_ptr<const std::string>;

        /**
         * @brief Sends the start of @p p_bytes to the client.
         * @return how many bytes it took, 0 when it can take none right now, or @c std::nullopt when the connection failed
         */
        using sender = std::function<std::optional<std::size_t>(std::string_view p_bytes)>;

        explicit client_outbox(const std::size_t p_capacity) noexcept : m_capacity(std::max<std::size_t>(p_capacity, 1)) {}

        void push(line p_line) {
            m_lines.push_back(std::move(p_line));
            const std::size_t v_in_flight = m_front_written > 0 ? 1 : 0;
            while (m_lines.size() - v_in_flight > m_capacity) {
                m_lines.erase(std::next(m_lines.begin(), static_cast<std::ptrdiff_t>(v_in_flight)));
            }
        }

        /**
         * @brief Sends queued lines through @p p_send, oldest first, until it takes nothing more or the queue is empty.
         * @return false when @p p_send reported a failed connection
         */
        bool flush(const sender& p_send) {
            while (!m_lines.empty()) {
                const std::string_view v_line = *m_lines.front();
                const std::optional<std::size_t> v_sent = p_send(v_line.substr(m_front_written));
                if (!v_sent) {
                    return false;
                }
                if (*v_sent == 0) {
                    return true;
                }
                m_front_written += *v_sent;
                if (m_front_written == v_line.size()) {
                    m_lines.pop_front();
                    m_front_written = 0;
                }
            }
            return true;
        }

        [[nodiscard]] bool empty() const noexcept { return m_lines.empty(); }

    private:
        std::deque<line> m_lines;
        std::size_t m_front_written = 0;
        std::size_t m_capacity;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_CLIENT_OUTBOX_HPP
