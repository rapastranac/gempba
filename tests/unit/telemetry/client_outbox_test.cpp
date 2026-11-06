#include <gtest/gtest.h>
#include <impl/telemetry/client_outbox.hpp>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

    using gempba::telemetry::client_outbox;

    client_outbox::line make_line(std::string p_text) { return std::make_shared<const std::string>(std::move(p_text)); }

    /// Takes up to @c m_budget more bytes, then reports a full send buffer; reports a failed connection while @c m_failed is set.
    struct fake_socket {
        std::string m_received;
        std::size_t m_budget = std::numeric_limits<std::size_t>::max();
        bool m_failed = false;

        std::optional<std::size_t> take(const std::string_view p_bytes) {
            if (m_failed) {
                return std::nullopt;
            }
            const std::size_t v_taken = std::min(p_bytes.size(), m_budget);
            m_received.append(p_bytes.substr(0, v_taken));
            m_budget -= v_taken;
            return v_taken;
        }
    };

    client_outbox::sender sender_into(fake_socket& p_socket) {
        return [&p_socket](const std::string_view p_bytes) { return p_socket.take(p_bytes); };
    }

    TEST(client_outbox_test, sends_every_line_in_order) {
        client_outbox v_outbox(16);
        fake_socket v_socket;
        v_outbox.push(make_line("a\n"));
        v_outbox.push(make_line("b\n"));
        v_outbox.push(make_line("c\n"));

        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("a\nb\nc\n", v_socket.m_received);
        EXPECT_TRUE(v_outbox.empty());
    }

    TEST(client_outbox_test, stops_when_the_socket_is_full_and_resumes_mid_line) {
        client_outbox v_outbox(16);
        fake_socket v_socket;
        v_outbox.push(make_line("hello\n"));
        v_outbox.push(make_line("world\n"));
        v_socket.m_budget = 3;

        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("hel", v_socket.m_received);
        EXPECT_FALSE(v_outbox.empty());

        v_socket.m_budget = std::numeric_limits<std::size_t>::max();
        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("hello\nworld\n", v_socket.m_received);
        EXPECT_TRUE(v_outbox.empty());
    }

    TEST(client_outbox_test, keeps_only_the_newest_lines_past_its_capacity) {
        client_outbox v_outbox(2);
        fake_socket v_socket;
        for (const char* v_text: {"1\n", "2\n", "3\n", "4\n"}) {
            v_outbox.push(make_line(v_text));
        }

        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("3\n4\n", v_socket.m_received);
    }

    TEST(client_outbox_test, never_drops_a_line_it_has_started_to_send) {
        client_outbox v_outbox(1);
        fake_socket v_socket;
        v_outbox.push(make_line("first\n"));
        v_socket.m_budget = 2;
        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));

        v_outbox.push(make_line("second\n"));
        v_outbox.push(make_line("third\n"));
        v_socket.m_budget = std::numeric_limits<std::size_t>::max();

        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("first\nthird\n", v_socket.m_received);
    }

    TEST(client_outbox_test, a_capacity_of_zero_still_keeps_the_newest_line) {
        client_outbox v_outbox(0);
        fake_socket v_socket;
        v_outbox.push(make_line("old\n"));
        v_outbox.push(make_line("new\n"));

        EXPECT_TRUE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_EQ("new\n", v_socket.m_received);
    }

    TEST(client_outbox_test, reports_a_failed_connection) {
        client_outbox v_outbox(16);
        fake_socket v_socket;
        v_socket.m_failed = true;
        v_outbox.push(make_line("a\n"));

        EXPECT_FALSE(v_outbox.flush(sender_into(v_socket)));
        EXPECT_FALSE(v_outbox.empty());
    }

} // namespace
