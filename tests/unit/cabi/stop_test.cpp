#include <cabi_test_fixture.hpp>
#include <csignal>
#include <impl/stop_state.hpp>

namespace gempba::cabi_tests {

    class cabi_stop_test : public cabi_fixture {};

    TEST_F(cabi_stop_test, no_stop_is_requested_at_first) { EXPECT_EQ(0, gempba_stop_requested()); }

    TEST_F(cabi_stop_test, request_stop_interrupts_by_code) {
        gempba_request_stop();

        EXPECT_EQ(1, gempba_stop_requested());
        EXPECT_TRUE(gempba::stop_requested());
    }

    TEST_F(cabi_stop_test, a_signal_the_host_caught_is_a_stop_by_that_signal_until_shutdown) {
        gempba_request_stop_on_signal(SIGTERM);

        EXPECT_EQ((gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGTERM}), gempba::detail::requested_stop());
        gempba::shutdown();
        EXPECT_EQ(0, gempba_stop_requested());
    }

    TEST_F(cabi_stop_test, stop_on_signals_turns_taking_the_signals_off_and_on) {
        gempba_telemetry_disable();
        gempba_stop_on_signals(0);
        gempba::create_node_manager(gempba::create_load_balancer(gempba::QUASI_HORIZONTAL));
        const auto v_off = std::signal(SIGTERM, SIG_DFL);

        gempba_stop_on_signals(1);
        const auto v_on = std::signal(SIGTERM, SIG_DFL);
        std::signal(SIGTERM, v_on);

        EXPECT_EQ(SIG_DFL, v_off);
        EXPECT_NE(SIG_DFL, v_on);
    }

    static_assert(GEMPBA_END_COMPLETED == static_cast<int>(gempba::end_reason::COMPLETED));
    static_assert(GEMPBA_END_INTERRUPTED == static_cast<int>(gempba::end_reason::INTERRUPTED));
    static_assert(GEMPBA_STOP_NONE == static_cast<int>(gempba::stop_trigger::NONE));
    static_assert(GEMPBA_STOP_CODE == static_cast<int>(gempba::stop_trigger::CODE));
    static_assert(GEMPBA_STOP_SIGNAL == static_cast<int>(gempba::stop_trigger::SIGNAL));
    static_assert(GEMPBA_STOP_CONTROL == static_cast<int>(gempba::stop_trigger::CONTROL));

} // namespace gempba::cabi_tests
