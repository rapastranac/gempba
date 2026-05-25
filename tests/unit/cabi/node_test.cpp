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

    // ─── dummy node ─────────────────────────────────────────────────────────

    TEST_F(cabi_node_test, dummy_node_create_and_destroy) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_QUASI_HORIZONTAL);
        ASSERT_NE(v_lb, nullptr);

        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_node), GEMPBA_OK);
        ASSERT_NE(v_node, nullptr);

        gempba_node_destroy(v_node);
    }

    TEST_F(cabi_node_test, node_destroy_on_null_handle_is_safe) {
        // `delete nullptr` is a well-defined no-op in C++.
        gempba_node_destroy(nullptr);
        SUCCEED();
    }

    // ─── seed node ──────────────────────────────────────────────────────────

    TEST_F(cabi_node_test, seed_node_lifecycle) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);

        cb_state v_state;
        const uint8_t v_payload[] = {0xAA, 0xBB};
        gempba_bytes_t v_args{v_payload, sizeof(v_payload)};

        gempba_node_t v_seed = nullptr;
        ASSERT_EQ(gempba_create_seed_node(v_lb, copy_args_runnable, &v_state, increment_release, v_args, &v_seed), GEMPBA_OK);
        ASSERT_NE(v_seed, nullptr);

        // Without submitting we exit; release_fn still fires exactly once when
        // the node is destroyed (same ownership contract as explicit nodes).
        gempba_node_destroy(v_seed);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_node_test, seed_create_null_out_releases_user_data) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);

        cb_state v_state;
        EXPECT_EQ(gempba_create_seed_node(v_lb, copy_args_runnable, &v_state, increment_release, gempba_bytes_t{nullptr, 0}, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    // ─── multithreading explicit node ───────────────────────────────────────

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

    TEST_F(cabi_node_test, mt_explicit_create_null_parent_releases_user_data) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);

        cb_state v_state;
        gempba_node_t v_out = nullptr;
        EXPECT_EQ(gempba_mt_create_explicit_node(v_lb, /*parent=*/nullptr, copy_args_runnable, &v_state, increment_release, gempba_bytes_t{nullptr, 0}, &v_out), GEMPBA_ERR_INVALID_ARG);
        EXPECT_EQ(v_out, nullptr);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    // ─── multithreading lazy node ───────────────────────────────────────────

    namespace {
        // Lazy producer that signals "no more args" on every call.
        gempba_status_t no_args_lazy(void* /*user_data*/, gempba_bool_t* p_produced, gempba_buffer_t* /*out_args*/) {
            *p_produced = 0;
            return GEMPBA_OK;
        }

        /** Lazy producer that yields a single packet on the first call and
         * signals "no more" on subsequent calls. Counts invocations through
         * cb_state::m_calls so the test can verify the producer actually ran.
         */
        gempba_status_t one_shot_lazy(void* p_user_data, gempba_bool_t* p_produced, gempba_buffer_t* p_out_args) {
            auto* s = static_cast<cb_state*>(p_user_data);
            const int v_calls = s->m_calls.fetch_add(1, std::memory_order_relaxed);
            if (v_calls > 0) {
                *p_produced = 0;
                return GEMPBA_OK;
            }
            *p_out_args = gempba_buffer_alloc(2);
            if (p_out_args->data == nullptr)
                return GEMPBA_ERR_OUT_OF_MEMORY;
            p_out_args->data[0] = 0xC0;
            p_out_args->data[1] = 0xDE;
            *p_produced = 1;
            return GEMPBA_OK;
        }
    } // namespace

    TEST_F(cabi_node_test, mt_lazy_node_lifecycle) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        gempba_node_t v_lazy = nullptr;
        ASSERT_EQ(gempba_mt_create_lazy_node(v_lb, v_parent, copy_args_runnable, no_args_lazy, &v_state, increment_release, &v_lazy), GEMPBA_OK);
        ASSERT_NE(v_lazy, nullptr);

        // Lazy children form a parent↔child shared_ptr cycle. We must submit
        // and wait so the load_balancer drains the work and releases its
        // references — otherwise the cycle persists and the holder's
        // release_fn never fires.
        gempba_bool_t v_accepted = 0;
        ASSERT_EQ(gempba_nm_try_local_submit(v_nm, v_lazy, &v_accepted), GEMPBA_OK);
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        gempba_node_destroy(v_lazy);
        gempba_node_destroy(v_parent);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_node_test, mt_lazy_create_null_lazy_args_releases_user_data) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        gempba_node_t v_out = nullptr;
        EXPECT_EQ(gempba_mt_create_lazy_node(v_lb, v_parent, copy_args_runnable, /*lazy_args=*/nullptr, &v_state, increment_release, &v_out), GEMPBA_ERR_INVALID_ARG);
        EXPECT_EQ(v_out, nullptr);
        EXPECT_EQ(v_state.m_releases.load(), 1);

        gempba_node_destroy(v_parent);
    }

    TEST_F(cabi_node_test, mt_lazy_node_runs_with_produced_args) {
        // Exercises the make_cpp_lazy_args produced=1 branch — the producer
        // hands back an owned buffer that the C ABI must convert to a
        // task_packet and route into the runnable. Without this, only the
        // produced=0 branch (mt_lazy_node_lifecycle) is reached.
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        gempba_node_t v_lazy = nullptr;
        ASSERT_EQ(gempba_mt_create_lazy_node(v_lb, v_parent, copy_args_runnable, one_shot_lazy, &v_state, increment_release, &v_lazy), GEMPBA_OK);

        gempba_bool_t v_accepted = 0;
        ASSERT_EQ(gempba_nm_try_local_submit(v_nm, v_lazy, &v_accepted), GEMPBA_OK);
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        // The producer must have been called and the runnable must have
        // observed the two bytes the producer emitted.
        EXPECT_GT(v_state.m_calls.load(), 0);
        ASSERT_EQ(v_state.m_last_args.size(), 2u);
        EXPECT_EQ(v_state.m_last_args[0], 0xC0);
        EXPECT_EQ(v_state.m_last_args[1], 0xDE);

        gempba_node_destroy(v_lazy);
        gempba_node_destroy(v_parent);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    // ─── multiprocessing explicit node (MP/DEV builds) ──────────────────────

    TEST_F(cabi_node_test, mp_create_explicit_node_null_parent_releases_user_data) {
        gempba_load_balancer_t v_lb = gempba_mp_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING, /*worker=*/nullptr);
        ASSERT_NE(v_lb, nullptr);

        cb_state v_state;
        gempba_node_t v_out = nullptr;
        EXPECT_EQ(gempba_mp_create_explicit_node(v_lb, /*parent=*/nullptr, copy_args_runnable, &v_state, increment_release, gempba_bytes_t{nullptr, 0}, &v_out), GEMPBA_ERR_INVALID_ARG);
        EXPECT_EQ(v_out, nullptr);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    TEST_F(cabi_node_test, mp_create_explicit_node_happy_path_round_trips_bytes) {
        // In an MP/DEV build the gempba_mp_create_explicit_node body is its
        // own code path (separate identity_ser / identity_deser_value bridges
        // from the MT one). This test pins the MP shim end-to-end without
        // needing MPI.
        gempba_load_balancer_t v_lb = gempba_mp_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING, /*worker=*/nullptr);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mp_create_node_manager(v_lb, /*worker=*/nullptr);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        const uint8_t v_payload[] = {0x11, 0x22, 0x33};
        gempba_bytes_t v_args{v_payload, sizeof(v_payload)};
        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mp_create_explicit_node(v_lb, v_parent, copy_args_runnable, &v_state, increment_release, v_args, &v_child), GEMPBA_OK);
        ASSERT_NE(v_child, nullptr);

        gempba_bool_t v_accepted = 0;
        ASSERT_EQ(gempba_nm_try_local_submit(v_nm, v_child, &v_accepted), GEMPBA_OK);
        ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);

        gempba_buffer_t v_out{nullptr, 0};
        ASSERT_EQ(gempba_node_get_result(v_child, &v_out), GEMPBA_OK);
        ASSERT_EQ(v_out.len, sizeof(v_payload));
        EXPECT_EQ(std::memcmp(v_out.data, v_payload, sizeof(v_payload)), 0);
        gempba_buffer_free(&v_out);

        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);
        EXPECT_EQ(v_state.m_releases.load(), 1);
    }

    // ─── node_get_result branches ───────────────────────────────────────────

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

    TEST_F(cabi_node_test, node_get_result_null_out_returns_invalid_arg) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_node), GEMPBA_OK);

        EXPECT_EQ(gempba_node_get_result(v_node, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_NE(gempba_last_error_message(), nullptr);

        gempba_node_destroy(v_node);
    }

    TEST_F(cabi_node_test, node_get_result_returns_empty_for_dummy_node) {
        // A dummy node has no runnable, so its future never resolves with a
        // value — the std::any unwraps as empty and the C ABI returns OK +
        // {nullptr, 0}.
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_t v_node = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_node), GEMPBA_OK);

        gempba_buffer_t v_out{reinterpret_cast<uint8_t*>(0x1), 99}; // sentinel
        ASSERT_EQ(gempba_node_get_result(v_node, &v_out), GEMPBA_OK);
        EXPECT_EQ(v_out.data, nullptr);
        EXPECT_EQ(v_out.len, 0u);

        gempba_node_destroy(v_node);
    }

    // ─── runnable returning error status ────────────────────────────────────

    namespace {
        gempba_status_t failing_runnable(void* /*user_data*/, uint64_t /*tid*/, gempba_bytes_t /*args*/, gempba_node_t /*parent*/, gempba_buffer_t* p_out_result) {
            // Allocate a buffer before returning failure to exercise the
            // buffer-free branch inside make_cpp_runnable's error handler.
            *p_out_result = gempba_buffer_alloc(4);
            if (p_out_result->data == nullptr)
                return GEMPBA_ERR_OUT_OF_MEMORY;
            return GEMPBA_ERR_RUNTIME;
        }
    } // namespace

    TEST_F(cabi_node_test, runnable_returning_error_status_surfaces_through_get_result) {
        // Hits two branches: (1) the user runnable returns != GEMPBA_OK after
        // writing into c_out, so make_cpp_runnable frees and throws; (2) the
        // exception propagates through node_core's future and is re-raised
        // when gempba_node_get_result calls get_any_result.
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        const uint8_t v_payload[] = {0xEE};
        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mt_create_explicit_node(v_lb, v_parent, failing_runnable, &v_state, increment_release, gempba_bytes_t{v_payload, sizeof(v_payload)}, &v_child), GEMPBA_OK);

        // try_local_submit may queue the task (OK; the throw is captured in
        // the node's future and resurfaces from gempba_node_get_result) or —
        // if the thread pool decides to run it inline via forward — propagate
        // the throw synchronously and return ERR_RUNTIME. The branch chosen
        // depends on the pool's current load, which carries over between
        // tests.
        gempba_bool_t v_accepted = 0;
        const gempba_status_t v_submit_rc = gempba_nm_try_local_submit(v_nm, v_child, &v_accepted);
        if (v_submit_rc == GEMPBA_OK) {
            ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);
            gempba_buffer_t v_out{nullptr, 0};
            EXPECT_NE(gempba_node_get_result(v_child, &v_out), GEMPBA_OK);
            gempba_buffer_free(&v_out);
        } else {
            EXPECT_EQ(v_submit_rc, GEMPBA_ERR_RUNTIME);
        }
        EXPECT_NE(gempba_last_error_message(), nullptr);

        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);
        // The release-fn contract is intentionally NOT asserted here.
        // When the runnable throws, the load_balancer's branch chosen by
        // try_local_submit decides whether the parent↔child shared_ptr
        // cycle gets broken: send() calls prune() before delegating (cycle
        // breaks), but forward() runs the runnable inline and skips prune()
        // when run() throws (cycle survives until shutdown). Which branch
        // is taken is timing-dependent under load — covered cleanly by
        // mt_explicit_invokes_callback_and_releases_user_data_once on the
        // success path, where it can be asserted deterministically.
    }

    // ─── runnable throws across the boundary (defensive ABI catches) ────────
    //
    // Bindings MUST NOT throw across the C ABI per the documented contract,
    // but if one does the ABI must translate the exception into a sensible
    // gempba_status_t code, never crash.  These tests deliberately violate
    // the contract to pin the GEMPBA_CATCH_RETURN_STATUS branches.
    //
    // The status code depends on which path try_local_submit picks:
    //   - Queued path (submit returns OK): the gempba task wrapper captures
    //     the throw, then future.get() inside gempba_node_get_result re-raises
    //     it as a normalised std::exception subclass → C ABI's catch-
    //     std::exception branch fires → GEMPBA_ERR_RUNTIME.
    //   - Inline-forward path (submit propagates synchronously): the throw
    //     bubbles straight to try_local_submit's catch envelope, so the
    //     specific catch-std::bad_alloc / catch (...) branch fires directly
    //     → GEMPBA_ERR_OUT_OF_MEMORY / GEMPBA_ERR_UNKNOWN.
    // Both paths are valid outcomes — the test accepts either.

    namespace {
        gempba_status_t throws_bad_alloc(void*, uint64_t, gempba_bytes_t, gempba_node_t, gempba_buffer_t*) { throw std::bad_alloc{}; }

        gempba_status_t throws_unknown(void*, uint64_t, gempba_bytes_t, gempba_node_t, gempba_buffer_t*) {
            throw 42; // NOLINT(hicpp-exception-baseclass) — non-std::exception type exercises catch (...)
        }
    } // namespace

    /**
     * Drives a runnable through submit + get_result and asserts the C ABI
     * surfaces either `p_inline_expected` (when the load balancer ran it
     * synchronously via forward()) or `p_queued_expected` (when it queued
     * to the thread pool and the future re-threw on get_result).
     */
    static void expect_runnable_translates_to(gempba_runnable_fn p_runnable, gempba_status_t p_inline_expected, gempba_status_t p_queued_expected) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        gempba_nm_set_thread_pool_size(v_nm, 2);

        gempba_node_t v_parent = nullptr;
        ASSERT_EQ(gempba_create_dummy_node(v_lb, &v_parent), GEMPBA_OK);

        cb_state v_state;
        gempba_node_t v_child = nullptr;
        ASSERT_EQ(gempba_mt_create_explicit_node(v_lb, v_parent, p_runnable, &v_state, increment_release, gempba_bytes_t{nullptr, 0}, &v_child), GEMPBA_OK);

        gempba_bool_t v_accepted = 0;
        const gempba_status_t v_submit_rc = gempba_nm_try_local_submit(v_nm, v_child, &v_accepted);
        if (v_submit_rc == GEMPBA_OK) {
            ASSERT_EQ(gempba_nm_wait(v_nm), GEMPBA_OK);
            gempba_buffer_t v_out{nullptr, 0};
            EXPECT_EQ(gempba_node_get_result(v_child, &v_out), p_queued_expected);
            gempba_buffer_free(&v_out);
        } else {
            EXPECT_EQ(v_submit_rc, p_inline_expected);
        }

        gempba_node_destroy(v_child);
        gempba_node_destroy(v_parent);
    }

    TEST_F(cabi_node_test, runnable_throwing_bad_alloc_maps_to_out_of_memory_or_runtime) { expect_runnable_translates_to(throws_bad_alloc, GEMPBA_ERR_OUT_OF_MEMORY, GEMPBA_ERR_RUNTIME); }

    TEST_F(cabi_node_test, runnable_throwing_non_std_type_maps_to_unknown_or_runtime) { expect_runnable_translates_to(throws_unknown, GEMPBA_ERR_UNKNOWN, GEMPBA_ERR_RUNTIME); }

} // namespace gempba::cabi_tests
