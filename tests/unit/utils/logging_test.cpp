#include <format>
#include <gempba/utils/tree.hpp>
#include <gempba/utils/utils.hpp>
#include <gtest/gtest.h>
#include <log_capture.hpp>
#include <string>
#include <string_view>

namespace {
    struct counted_format {
        int* m_formats;
    };
} // namespace

template<>
struct std::formatter<counted_format> : std::formatter<std::string_view> {
    auto format(const counted_format& p_value, std::format_context& p_context) const {
        ++*p_value.m_formats;
        return std::formatter<std::string_view>::format("counted", p_context);
    }
};

namespace {

    TEST(logging_test, log_debug_writes_only_when_debug_logging_is_on) {
        log_capture v_capture;
        utils::log_debug("rank {}, {}", 2, std::string_view{"ready"});
        v_capture.set_level(spdlog::level::info);
        utils::log_debug("rank {}, {}", 5, std::string_view{"hidden"});

        EXPECT_NE(std::string::npos, v_capture.text().find("[debug] rank 2, ready")) << v_capture.text();
        EXPECT_EQ(std::string::npos, v_capture.text().find("hidden")) << v_capture.text();
    }

    TEST(logging_test, log_debug_formats_nothing_when_debug_logging_is_off) {
        log_capture v_capture;
        int v_formats = 0;
        v_capture.set_level(spdlog::level::info);
        utils::log_debug("{}", counted_format{&v_formats});
        EXPECT_EQ(0, v_formats);

        v_capture.set_level(spdlog::level::debug);
        utils::log_debug("{}", counted_format{&v_formats});
        EXPECT_EQ(1, v_formats);
        EXPECT_NE(std::string::npos, v_capture.text().find("[debug] counted")) << v_capture.text();
    }

    TEST(logging_test, log_error_writes_through_the_logger) {
        log_capture v_capture;
        utils::log_error("an error message");

        EXPECT_NE(std::string::npos, v_capture.text().find("[error] an error message")) << v_capture.text();
    }

    TEST(logging_test, log_and_throw_logs_the_formatted_message_and_throws_it) {
        log_capture v_capture;
        EXPECT_THROW(utils::log_and_throw("rank {} failed: {}", 3, "timeout"), std::runtime_error);
        EXPECT_NE(std::string::npos, v_capture.text().find("[error] rank 3 failed: timeout")) << v_capture.text();
    }

    TEST(logging_test, print_ipc_debug_comments_logs_only_when_debug_logging_is_on) {
#if GEMPBA_DEBUG_COMMENTS
        log_capture v_capture;
        utils::print_ipc_debug_comments("rank {}, {}", 2, "ready");
        v_capture.set_level(spdlog::level::info);
        utils::print_ipc_debug_comments("rank {}, {}", 5, "hidden");

        EXPECT_NE(std::string::npos, v_capture.text().find("[debug] rank 2, ready")) << v_capture.text();
        EXPECT_EQ(std::string::npos, v_capture.text().find("hidden")) << v_capture.text();
#else
        GTEST_SKIP() << "print_ipc_debug_comments logs only in builds with GEMPBA_DEBUG_COMMENTS";
#endif
    }

    TEST(logging_test, build_topology_logs_each_child_when_debug_logging_is_on) {
        log_capture v_capture;
        auto v_tree = tree(4);
        utils::build_topology(v_tree, 1, 0, 2, 4);

        EXPECT_NE(std::string::npos, v_capture.text().find("[debug] process: 1, child: 2")) << v_capture.text();
        EXPECT_NE(std::string::npos, v_capture.text().find("[debug] process: 1, child: 3")) << v_capture.text();
    }

} // namespace
