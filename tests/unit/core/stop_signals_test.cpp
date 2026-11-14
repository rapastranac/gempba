#include <atomic>
#include <csignal>
#include <gtest/gtest.h>

#include <gempba/gempba.hpp>
#include <impl/stop_state.hpp>

namespace {

    std::atomic<int> g_own_handler_calls{0};

    extern "C" void count_own_handler_call(int) { ++g_own_handler_calls; }

    bool is_default(const int p_signal) {
#if defined(_WIN32)
        const auto v_current = std::signal(p_signal, SIG_DFL);
        std::signal(p_signal, v_current);
        return v_current == SIG_DFL;
#else
        struct sigaction v_current{};
        sigaction(p_signal, nullptr, &v_current);
        return (v_current.sa_flags & SA_SIGINFO) == 0 && v_current.sa_handler == SIG_DFL;
#endif
    }

    class stop_signals_test : public ::testing::Test {
    protected:
        void SetUp() override {
            gempba::telemetry::disable();
            g_own_handler_calls = 0;
        }

        void TearDown() override {
            gempba::shutdown();
            gempba::stop_on_signals(true);
            std::signal(SIGINT, SIG_DFL);
            std::signal(SIGTERM, SIG_DFL);
            gempba::telemetry::enable();
        }

        static void create_node_manager() { gempba::create_node_manager(gempba::create_load_balancer(gempba::QUASI_HORIZONTAL)); }
    };

} // namespace

TEST_F(stop_signals_test, creating_the_node_manager_takes_both_signals) {
    create_node_manager();

    EXPECT_FALSE(is_default(SIGINT));
    EXPECT_FALSE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, sigterm_requests_a_stop) {
    create_node_manager();
    ASSERT_FALSE(is_default(SIGTERM));

    std::raise(SIGTERM);

    EXPECT_EQ((gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGTERM}), gempba::detail::requested_stop());
}

TEST_F(stop_signals_test, sigterm_stays_taken_after_the_first) {
    create_node_manager();
    ASSERT_FALSE(is_default(SIGTERM));

    std::raise(SIGTERM);
    ASSERT_FALSE(is_default(SIGTERM));
    std::raise(SIGTERM);

    EXPECT_FALSE(is_default(SIGTERM));
    EXPECT_EQ(SIGTERM, gempba::detail::requested_stop().m_signal);
}

TEST_F(stop_signals_test, sigint_requests_a_stop_and_lets_a_second_one_kill) {
    create_node_manager();
    ASSERT_FALSE(is_default(SIGINT));

    std::raise(SIGINT);

    EXPECT_EQ((gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGINT}), gempba::detail::requested_stop());
    EXPECT_TRUE(is_default(SIGINT));
}

TEST_F(stop_signals_test, a_signal_with_its_own_handler_is_left_to_it) {
    std::signal(SIGTERM, count_own_handler_call);
    create_node_manager();

    std::raise(SIGTERM);

    EXPECT_EQ(1, g_own_handler_calls.load());
    EXPECT_FALSE(gempba::stop_requested());
}

TEST_F(stop_signals_test, an_ignored_signal_stays_ignored) {
    std::signal(SIGTERM, SIG_IGN);
    create_node_manager();

    std::raise(SIGTERM);

    EXPECT_FALSE(gempba::stop_requested());
}

TEST_F(stop_signals_test, shutdown_gives_both_signals_back) {
    create_node_manager();

    gempba::shutdown();

    EXPECT_TRUE(is_default(SIGINT));
    EXPECT_TRUE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, resetting_the_node_manager_gives_both_signals_back) {
    create_node_manager();

    gempba::reset_node_manager();

    EXPECT_TRUE(is_default(SIGINT));
    EXPECT_TRUE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, turned_off_the_node_manager_takes_no_signal) {
    gempba::stop_on_signals(false);

    create_node_manager();

    EXPECT_TRUE(is_default(SIGINT));
    EXPECT_TRUE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, turning_it_off_gives_both_signals_back_at_once) {
    create_node_manager();

    gempba::stop_on_signals(false);

    EXPECT_TRUE(is_default(SIGINT));
    EXPECT_TRUE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, turning_it_back_on_takes_both_signals_again) {
    create_node_manager();
    gempba::stop_on_signals(false);

    gempba::stop_on_signals(true);

    EXPECT_FALSE(is_default(SIGINT));
    EXPECT_FALSE(is_default(SIGTERM));
}

TEST_F(stop_signals_test, signal_names) {
    EXPECT_EQ("SIGINT", gempba::detail::signal_name(SIGINT));
    EXPECT_EQ("SIGTERM", gempba::detail::signal_name(SIGTERM));
    EXPECT_EQ("99", gempba::detail::signal_name(99));
}
