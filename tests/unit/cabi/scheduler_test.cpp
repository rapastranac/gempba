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
 *
 * Tests for the gempba_scheduler_* surface.
 *
 * The C ABI's job here is to translate plain C calls into virtual dispatches
 * on a gempba::scheduler instance held inside the wrapper struct.  Full
 * scheduler behaviour requires a live MPI process group, but the translation
 * itself does not — we build a scheduler_mock against the gempba::scheduler
 * interface and verify each C ABI entry point fires the right virtual with
 * the right argument transformation.
 */

#include <gmock/gmock.h>

#include <cabi/internals.hpp>
#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    using ::testing::Return;

    class cabi_scheduler_test : public cabi_fixture {};

    // ─── No-handle / null-arg guards ────────────────────────────────────────

    TEST_F(cabi_scheduler_test, create_null_out_returns_invalid_arg) {
        EXPECT_EQ(gempba_scheduler_create(GEMPBA_TOPOLOGY_CENTRALIZED, /*timeout=*/0.0, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }

    TEST_F(cabi_scheduler_test, destroy_null_is_safe) {
        gempba_scheduler_destroy(nullptr);
        SUCCEED();
    }

    // ─── GMock-backed dispatch tests ────────────────────────────────────────
    //
    // We don't test that gempba's real scheduler works (that's the C++ test
    // suite's job).  We test that the C ABI correctly:
    //   - resolves the wrapper handle to the underlying gempba::scheduler*
    //   - invokes the right virtual method
    //   - translates argument enums (gempba_goal_t / gempba_score_type_t) into
    //     the matching gempba::goal / gempba::score_type values
    //   - returns the right type-cast of the C++ result

    class scheduler_mock : public gempba::scheduler {
    public:
        // scheduler_traits
        MOCK_METHOD(void, barrier, (), (override));
        MOCK_METHOD(int, rank_me, (), (const, override));
        MOCK_METHOD(int, world_size, (), (const, override));
        MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), (const, override));

        // scheduler
        MOCK_METHOD(double, elapsed_time, (), (const, override));
        MOCK_METHOD(std::size_t, get_pending_request_count, (), (const, override));
        MOCK_METHOD(void, set_goal, (gempba::goal, gempba::score_type), (override));
        MOCK_METHOD(void, set_custom_initial_topology, (tree&&), (override));
        MOCK_METHOD(std::vector<std::unique_ptr<gempba::stats>>, get_stats_vector, (), (const, override));
        MOCK_METHOD(void, synchronize_stats, (), (override));
        MOCK_METHOD(gempba::scheduler::center&, center_view, (), (override));
        MOCK_METHOD(gempba::scheduler::worker&, worker_view, (), (override));
    };

    /** Wraps a scheduler_mock in the C ABI handle shape so tests can call the C entry points. */
    static gempba_scheduler_t handle_for(scheduler_mock& p_mock, gempba_scheduler_s& p_storage) {
        p_storage.cpp = &p_mock;
        return reinterpret_cast<gempba_scheduler_t>(&p_storage);
    }

    TEST_F(cabi_scheduler_test, barrier_dispatches_to_underlying_scheduler) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, barrier()).Times(1);
        gempba_scheduler_barrier(handle_for(v_mock, v_s));
    }

    TEST_F(cabi_scheduler_test, rank_me_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, rank_me()).WillOnce(Return(7));
        EXPECT_EQ(gempba_scheduler_rank_me(handle_for(v_mock, v_s)), 7);
    }

    TEST_F(cabi_scheduler_test, world_size_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, world_size()).WillOnce(Return(4));
        EXPECT_EQ(gempba_scheduler_world_size(handle_for(v_mock, v_s)), 4);
    }

    TEST_F(cabi_scheduler_test, elapsed_time_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, elapsed_time()).WillOnce(Return(12.5));
        EXPECT_DOUBLE_EQ(gempba_scheduler_elapsed_time(handle_for(v_mock, v_s)), 12.5);
    }

    TEST_F(cabi_scheduler_test, synchronize_stats_dispatches_to_underlying_scheduler) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, synchronize_stats()).Times(1);
        gempba_scheduler_synchronize_stats(handle_for(v_mock, v_s));
    }

    TEST_F(cabi_scheduler_test, set_goal_translates_enums_to_underlying_types) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, set_goal(gempba::MAXIMISE, gempba::score_type::I32)).Times(1);
        gempba_scheduler_set_goal(handle_for(v_mock, v_s), GEMPBA_GOAL_MAXIMISE, GEMPBA_SCORE_I32);

        EXPECT_CALL(v_mock, set_goal(gempba::MINIMISE, gempba::score_type::F64)).Times(1);
        gempba_scheduler_set_goal(handle_for(v_mock, v_s), GEMPBA_GOAL_MINIMISE, GEMPBA_SCORE_F64);
    }

} // namespace gempba::cabi_tests
