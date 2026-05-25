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
 * Tests for the gempba_serial_runnable_* surface — the MP-only helper a
 * binding hands to a scheduler worker so a center-node task can be invoked
 * locally on a worker rank.  Covers both the returns-value and the void
 * (fire-and-forget) variants, plus the null-arg release-fn contract.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_serial_runnable_test : public cabi_fixture {};

    TEST_F(cabi_serial_runnable_test, non_void_invoke_round_trips_bytes) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);

        cb_state v_state;
        gempba_serial_runnable_t v_sr = nullptr;
        ASSERT_EQ(gempba_serial_runnable_create(/*id=*/7, /*returns_value=*/1, copy_args_runnable, &v_state, increment_release, &v_sr), GEMPBA_OK);
        ASSERT_NE(v_sr, nullptr);

        const uint8_t v_payload[] = {0x01, 0x02, 0x03};
        gempba_bytes_t v_args{v_payload, sizeof(v_payload)};

        gempba_buffer_t v_out{nullptr, 0};
        ASSERT_EQ(gempba_serial_runnable_invoke(v_sr, v_nm, v_args, &v_out), GEMPBA_OK);
        ASSERT_EQ(v_out.len, sizeof(v_payload));
        EXPECT_EQ(std::memcmp(v_out.data, v_payload, sizeof(v_payload)), 0);
        gempba_buffer_free(&v_out);

        // The invoke blocks on the result future, but the worker thread may
        // still be holding its local copy of the std::function for a beat.
        // Drain the pool before destroying the runnable so the release_fn
        // counter is observed deterministically under CPU contention.
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);
        gempba_serial_runnable_destroy(v_sr);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_serial_runnable_test, void_invoke_returns_no_value) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);

        cb_state v_state;
        gempba_serial_runnable_t v_sr = nullptr;
        ASSERT_EQ(gempba_serial_runnable_create(/*id=*/3, /*returns_value=*/0, copy_args_runnable, &v_state, increment_release, &v_sr), GEMPBA_OK);

        const uint8_t v_payload[] = {0xAB};
        gempba_buffer_t v_out{reinterpret_cast<uint8_t*>(0x1), 99}; // sentinel
        ASSERT_EQ(gempba_serial_runnable_invoke(v_sr, v_nm, gempba_bytes_t{v_payload, sizeof(v_payload)}, &v_out), GEMPBA_OK);
        EXPECT_EQ(v_out.data, nullptr);
        EXPECT_EQ(v_out.len, 0u);

        // The void variant is fire-and-forget — the runnable's captured
        // `this` is accessed by the still-running thread-pool task, so we
        // must drain it before tearing the wrapper down or the worker will
        // use-after-free.
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);
        gempba_serial_runnable_destroy(v_sr);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_serial_runnable_test, create_null_out_releases_user_data) {
        cb_state v_state;
        EXPECT_EQ(gempba_serial_runnable_create(/*id=*/0, /*returns_value=*/1, copy_args_runnable, &v_state, increment_release, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_serial_runnable_test, invoke_null_args_return_invalid_arg) {
        gempba_buffer_t v_out{nullptr, 0};
        EXPECT_EQ(gempba_serial_runnable_invoke(/*sr=*/nullptr, /*nm=*/nullptr, gempba_bytes_t{nullptr, 0}, &v_out), GEMPBA_ERR_INVALID_ARG);
    }

    TEST_F(cabi_serial_runnable_test, destroy_null_is_safe) {
        gempba_serial_runnable_destroy(nullptr);
        SUCCEED();
    }

} // namespace gempba::cabi_tests
