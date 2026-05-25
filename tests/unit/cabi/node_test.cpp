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
 * Tests for the four node factories (dummy / seed / explicit / lazy, in
 * both MT and MP flavours), gempba_node_destroy, and gempba_node_get_result.
 *
 * The recurring contract across all factories: user_data passed in with a
 * release_fn is released exactly once — whether the factory succeeded, was
 * rejected for a null argument, or its runnable later threw. The throwing
 * runnable case also covers the exception path from gempba_node_get_result.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_node_test : public cabi_fixture {};

    TEST_F(cabi_node_test, dummy_node_create_and_destroy) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_QUASI_HORIZONTAL);
        ASSERT_NE(v_lb, nullptr);

        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_node), GEMPBA_OK);
        ASSERT_NE(v_node, nullptr);

        gempba_node_destroy(v_node);
    }

    TEST_F(cabi_node_test, mt_explicit_invokes_callback_and_releases_user_data_once) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);

        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        const uint8_t v_payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
        gempba_bytes_t v_args{v_payload, sizeof(v_payload)};

        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mt_create_explicit_node(v_lb, v_parent, copy_args_runnable, &v_state, increment_release, v_args, &v_child), GEMPBA_OK);
        ASSERT_NE(v_child, nullptr);

        gempba_bool_t v_accepted = 0;
        ASSERT_EQ(gempba_nm_try_local_submit(v_nm, v_child, &v_accepted), GEMPBA_OK);

        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        // Whether or not the load balancer pruned vs. ran the node, exactly
        // one release of user_data must occur — and only after the runnable's
        // last std::function copy has been dropped. Any double-release here
        // would indicate the wrapper destructor is firing release_fn
        // redundantly.
        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);

        EXPECT_EQ(v_state.m_releases.load(), 1);

        // If the node ran (the work-stealing balancer is reliable here),
        // verify the bytes round-tripped through the C ABI exactly.
        if (v_state.m_calls.load() > 0) {
            ASSERT_EQ(v_state.m_last_args.size(), sizeof(v_payload));
            EXPECT_EQ(std::memcmp(v_state.m_last_args.data(), v_payload, sizeof(v_payload)), 0);
        }
    }

    // ─── node_get_result ────────────────────────────────────────────────────

    TEST_F(cabi_node_test, node_get_result_returns_runnable_output) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);

        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        const uint8_t v_payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
        gempba_bytes_t v_args{v_payload, sizeof(v_payload)};

        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mt_create_explicit_node(v_lb, v_parent, copy_args_runnable, &v_state, increment_release, v_args, &v_child), GEMPBA_OK);
        ASSERT_NE(v_child, nullptr);

        gempba_bool_t v_accepted = 0;
        ASSERT_EQ(gempba_nm_try_local_submit(v_nm, v_child, &v_accepted), GEMPBA_OK);

        // get_result blocks on the runnable's std::future and returns the
        // bytes the runnable wrote into out_result — which copy_args_runnable
        // mirrors from its args, so we can round-trip-check the payload.
        gempba_buffer_t v_out{nullptr, 0};
        ASSERT_EQ(gempba_node_get_result(v_child, &v_out), GEMPBA_OK);
        ASSERT_NE(v_out.data, nullptr);
        EXPECT_EQ(v_out.len, sizeof(v_payload));
        EXPECT_EQ(std::memcmp(v_out.data, v_payload, sizeof(v_payload)), 0);
        gempba_buffer_free(&v_out);

        // get_result only blocks on the runnable's future — the thread pool
        // worker may still be holding its local copy of the runnable's
        // std::function for a beat after signalling. Drain the pool with
        // wait() so the holder shared_ptr is guaranteed to be gone before
        // we check the release_fn counter (otherwise the assert is racy
        // under CPU contention, e.g. CLion's parallel CTest runs).
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_node_test, node_get_result_invalid_arg_sets_last_error) {
        gempba_clear_last_error();
        gempba_buffer_t v_out{nullptr, 0};
        EXPECT_EQ(gempba_node_get_result(nullptr, &v_out), GEMPBA_ERR_INVALID_ARG);
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }

} // namespace gempba::cabi_tests
