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
 * Tests for gempba_mt_create_node_manager / gempba_mp_create_node_manager
 * and the full gempba_nm_* property-bag surface bindings drive once they
 * hold a node_manager handle: balancing policy / score / goal / result /
 * submission / wait / probe entry points.
 */

#include <algorithm>
#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_node_manager_test : public cabi_fixture {};

    // ─── Singleton / factory ────────────────────────────────────────────────

    TEST_F(cabi_node_manager_test, mt_create_returns_same_as_singleton) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(v_nm, gempba_get_node_manager());
    }

    TEST_F(cabi_node_manager_test, mp_create_with_null_worker_succeeds) {
        gempba_load_balancer_t v_lb = gempba_mp_create_load_balancer(GEMPBA_BALANCING_QUASI_HORIZONTAL, /*worker=*/nullptr);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mp_create_node_manager(v_lb, /*worker=*/nullptr);
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(v_nm, gempba_get_node_manager());
    }

    // ─── Property accessors / setters ───────────────────────────────────────

    TEST_F(cabi_node_manager_test, get_balancing_policy_reflects_load_balancer) {
        // gempba_nm_get_balancing_policy reads the load balancer's policy,
        // not a separately-settable node_manager field —
        // gempba_nm_set_balancing_policy writes to a node_manager member that
        // get does not consult. This test pins the current behaviour so any
        // future C++ change that decouples them can't slip through without
        // updating consumers.
        gempba_node_manager_t v_nm = make_node_manager(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(gempba_nm_get_balancing_policy(v_nm), GEMPBA_BALANCING_WORK_STEALING);

        // Calling the setter must not crash even though the getter ignores it.
        gempba_nm_set_balancing_policy(v_nm, GEMPBA_BALANCING_QUASI_HORIZONTAL);
    }

    TEST_F(cabi_node_manager_test, score_set_get_round_trip) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const int32_t v_score = 42;
        const auto v_raw = static_cast<int64_t>(v_score);
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_I32);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_I32);
    }

    /**
     * One round-trip per non-I32 score kind covers the score::from_raw branch for that kind plus the get_score_raw /
     * get_score_kind translation back across the ABI.  Values are arbitrary; what matters is bit-exact preservation.
     */
    TEST_F(cabi_node_manager_test, score_set_get_round_trip_u32) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const auto v_raw = static_cast<int64_t>(0xDEADBEEFu);
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_U_I32);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_U_I32);
    }

    TEST_F(cabi_node_manager_test, score_set_get_round_trip_i64) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const int64_t v_raw = 0x1234567890ABCDEFLL;
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_I64);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_I64);
    }

    TEST_F(cabi_node_manager_test, score_set_get_round_trip_u64) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const auto v_raw = static_cast<int64_t>(0xFEEDFACECAFEBEEFuLL);
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_U_I64);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_U_I64);
    }

    TEST_F(cabi_node_manager_test, score_set_get_round_trip_f32) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const float v_f32 = 3.1415927f;
        int64_t v_raw = 0;
        std::memcpy(&v_raw, &v_f32, sizeof(v_f32));
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_F32);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_F32);
    }

    TEST_F(cabi_node_manager_test, score_set_get_round_trip_f64) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        const double v_f64 = 2.718281828459045;
        int64_t v_raw = 0;
        std::memcpy(&v_raw, &v_f64, sizeof(v_f64));
        gempba_nm_set_score(v_nm, v_raw, GEMPBA_SCORE_F64);

        EXPECT_EQ(gempba_nm_get_score_raw(v_nm), v_raw);
        EXPECT_EQ(gempba_nm_get_score_kind(v_nm), GEMPBA_SCORE_F64);
    }

    TEST_F(cabi_node_manager_test, generate_unique_id_returns_distinct_values) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        constexpr int V_N = 16;
        std::vector<uint32_t> v_ids(V_N);
        for (int i = 0; i < V_N; ++i) {
            v_ids[i] = gempba_nm_generate_unique_id(v_nm);
        }
        // No two IDs should collide within a single node_manager.
        std::sort(v_ids.begin(), v_ids.end());
        EXPECT_EQ(std::unique(v_ids.begin(), v_ids.end()), v_ids.end());
    }

    TEST_F(cabi_node_manager_test, rank_me_returns_minus_one_with_no_scheduler) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        // node_manager::rank_me returns -1 when no scheduler is attached —
        // always the case in MT-only mode, and also in MP-mode before a
        // scheduler is created.
        EXPECT_EQ(gempba_nm_rank_me(v_nm), -1);
    }

    TEST_F(cabi_node_manager_test, thread_request_count_starts_at_zero) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(gempba_nm_thread_request_count(v_nm), 0);
    }

    TEST_F(cabi_node_manager_test, set_goal_does_not_crash) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        // No getter for goal in the C ABI; smoke-test that both directions
        // and every score kind go through cleanly.
        gempba_nm_set_goal(v_nm, GEMPBA_GOAL_MAXIMISE, GEMPBA_SCORE_I32);
        gempba_nm_set_goal(v_nm, GEMPBA_GOAL_MINIMISE, GEMPBA_SCORE_F64);
    }

    TEST_F(cabi_node_manager_test, wait_returns_ok_on_fresh_manager) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        // No work submitted yet — wait must return immediately with OK.
        EXPECT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);
    }

    TEST_F(cabi_node_manager_test, wall_time_returns_non_negative) {
        // Static — does not require a node_manager. Sanity-bound.
        EXPECT_GE(gempba_nm_wall_time(), 0.0);
    }

    TEST_F(cabi_node_manager_test, is_done_and_idle_time_callable_on_fresh_manager) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        // is_done returns a defined bool; idle_time is non-negative. The
        // point of this test is to hit the otherwise-unexercised entry
        // points.
        const gempba_bool_t v_done = gempba_nm_is_done(v_nm);
        EXPECT_TRUE(v_done == 0 || v_done == 1);
        EXPECT_GE(gempba_nm_idle_time(v_nm), 0.0);
    }

    // ─── Submission / forward null-arg branches ─────────────────────────────

    TEST_F(cabi_node_manager_test, try_local_submit_null_args_return_invalid_arg) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        gempba_bool_t v_accepted = 0;
        EXPECT_EQ(gempba_nm_try_local_submit(v_nm, /*node=*/nullptr, &v_accepted), GEMPBA_ERR_INVALID_ARG);

        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(gempba_get_load_balancer(), &v_node), GEMPBA_OK);
        EXPECT_EQ(gempba_nm_try_local_submit(v_nm, v_node, /*out_accepted=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        gempba_node_destroy(v_node);
    }

    TEST_F(cabi_node_manager_test, forward_null_node_returns_invalid_arg) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(gempba_nm_forward(v_nm, /*node=*/nullptr), GEMPBA_ERR_INVALID_ARG);
    }

    TEST_F(cabi_node_manager_test, try_remote_submit_null_args_return_invalid_arg) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        gempba_bool_t v_accepted = 0;
        EXPECT_EQ(gempba_nm_try_remote_submit(v_nm, /*node=*/nullptr, /*runnable_id=*/0, &v_accepted), GEMPBA_ERR_INVALID_ARG);

        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(gempba_get_load_balancer(), &v_node), GEMPBA_OK);
        EXPECT_EQ(gempba_nm_try_remote_submit(v_nm, v_node, 0, /*out_accepted=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        gempba_node_destroy(v_node);
    }

    TEST_F(cabi_node_manager_test, forward_executes_branchable_explicit_node) {
        // Work-stealing's load_balancer::forward runs the node inline when
        // it is UNUSED and should_branch. Hits the happy path of
        // gempba_nm_forward.
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 1);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        const uint8_t v_payload[] = {0x77};
        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mt_create_explicit_node(v_lb, v_parent, copy_args_runnable, &v_state, increment_release, gempba_bytes_t{v_payload, sizeof(v_payload)}, &v_child), GEMPBA_OK);

        ASSERT_EQ(gempba_nm_forward(v_nm, v_child), GEMPBA_OK);
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    // ─── Result / score round-trips ─────────────────────────────────────────

    TEST_F(cabi_node_manager_test, get_result_null_out_returns_invalid_arg) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(gempba_nm_get_result(v_nm, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
    }

    TEST_F(cabi_node_manager_test, get_result_returns_empty_when_no_result_set) {
        // Default-constructed node_manager holds m_result as the std::any
        // variant alternative, not task_packet — get_result_bytes returns
        // nullopt and the C ABI maps that to OK + {nullptr, 0}.
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);

        gempba_buffer_t v_out{reinterpret_cast<uint8_t*>(0x1), 99}; // sentinel
        ASSERT_EQ(gempba_nm_get_result(v_nm, &v_out), GEMPBA_OK);
        EXPECT_EQ(v_out.data, nullptr);
        EXPECT_EQ(v_out.len, 0u);
    }

    TEST_F(cabi_node_manager_test, try_update_result_then_get_round_trip) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        // Anchor goal so the default score is INT_MIN; any I32 improves it.
        gempba_nm_set_goal(v_nm, GEMPBA_GOAL_MAXIMISE, GEMPBA_SCORE_I32);

        const uint8_t v_payload[] = {0xCA, 0xFE, 0xBA, 0xBE};
        gempba_bytes_t v_bytes{v_payload, sizeof(v_payload)};
        ASSERT_EQ(gempba_nm_try_update_result(v_nm, v_bytes, /*raw_bits=*/100, GEMPBA_SCORE_I32), 1);

        // A worse score must NOT overwrite the stored result.
        ASSERT_EQ(gempba_nm_try_update_result(v_nm, gempba_bytes_t{nullptr, 0}, /*raw_bits=*/50, GEMPBA_SCORE_I32), 0);

        gempba_buffer_t v_out{nullptr, 0};
        ASSERT_EQ(gempba_nm_get_result(v_nm, &v_out), GEMPBA_OK);
        ASSERT_EQ(v_out.len, sizeof(v_payload));
        EXPECT_EQ(std::memcmp(v_out.data, v_payload, sizeof(v_payload)), 0);
        gempba_buffer_free(&v_out);
    }

    TEST_F(cabi_node_manager_test, try_update_score_accepts_improvement_only) {
        gempba_node_manager_t v_nm = make_node_manager();
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_goal(v_nm, GEMPBA_GOAL_MAXIMISE, GEMPBA_SCORE_I32);

        EXPECT_EQ(gempba_nm_try_update_score(v_nm, /*raw_bits=*/100, GEMPBA_SCORE_I32), 1);
        EXPECT_EQ(gempba_nm_try_update_score(v_nm, /*raw_bits=*/50, GEMPBA_SCORE_I32), 0);
        EXPECT_EQ(gempba_nm_try_update_score(v_nm, /*raw_bits=*/200, GEMPBA_SCORE_I32), 1);

        // try_update_score also invalidates the stored result.
        gempba_buffer_t v_out{reinterpret_cast<uint8_t*>(0x1), 99};
        ASSERT_EQ(gempba_nm_get_result(v_nm, &v_out), GEMPBA_OK);
        EXPECT_EQ(v_out.len, 0u);
    }

} // namespace gempba::cabi_tests
