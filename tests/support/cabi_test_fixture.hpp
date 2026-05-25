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
 * Shared fixture and helpers for the gempba C ABI test suites.
 *
 * The C ABI surface is wide enough to warrant per-area test files
 * (tests/unit/cabi/*_test.cpp), but every test starts from the same
 * preconditions: gempba's process-wide singletons reset and the telemetry
 * flag at its documented default. The base fixture, plus a small set of
 * runnable + user_data helpers used across more than one area, live here so
 * each per-area file stays tightly focused on what it actually exercises.
 */

#ifndef GEMPBA_TESTS_CABI_TEST_FIXTURE_HPP
#define GEMPBA_TESTS_CABI_TEST_FIXTURE_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <gempba/cabi/gempba.h>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>

namespace gempba::cabi_tests {

    /**
     * Base fixture: every C ABI test starts from a fully-reset gempba runtime
     * (no load_balancer / node_manager / scheduler singletons live) with the
     * telemetry flag back at its documented default (enabled). Per-area test
     * files inherit from this so each suite-name (the GTest fixture class)
     * reflects which C++ subsystem the ABI calls land on.
     */
    class cabi_fixture : public ::testing::Test {
    protected:
        void SetUp() override {
            gempba::shutdown();
            gempba::telemetry::enable();
        }
        void TearDown() override {
            gempba::shutdown();
            gempba::telemetry::enable();
        }
    };

    /**
     * State captured from inside a runnable. Used by tests that need to
     * assert how many times the runnable was invoked, how many times the
     * release_fn fired, and what bytes the runnable observed as args.
     */
    struct cb_state {
        std::atomic<int> m_calls{0};
        std::atomic<int> m_releases{0};
        std::vector<uint8_t> m_last_args;
    };

    /**
     * Mirrors its args to the result buffer and records what it saw on the
     * cb_state. Most happy-path tests use this to round-trip bytes through
     * the C ABI: pass payload as args, expect the same payload out.
     */
    inline gempba_status_t copy_args_runnable(void* p_user_data, uint64_t /*tid*/, gempba_bytes_t p_args, gempba_node_t /*parent*/, gempba_buffer_t* p_out_result) {
        auto* s = static_cast<cb_state*>(p_user_data);
        s->m_calls.fetch_add(1, std::memory_order_relaxed);

        if (p_args.len > 0 && p_args.data != nullptr) {
            s->m_last_args.assign(p_args.data, p_args.data + p_args.len);
        }

        *p_out_result = gempba_buffer_alloc(p_args.len);
        if (p_args.len > 0 && p_out_result->data == nullptr) {
            return GEMPBA_ERR_OUT_OF_MEMORY;
        }
        if (p_args.len > 0) {
            std::memcpy(p_out_result->data, p_args.data, p_args.len);
        }
        return GEMPBA_OK;
    }

    /** release_fn that bumps a counter — pair with cb_state to verify the
     * "exactly once" contract that safe_make_holder guarantees. */
    inline void increment_release(void* p_user_data) { static_cast<cb_state*>(p_user_data)->m_releases.fetch_add(1, std::memory_order_relaxed); }

} // namespace gempba::cabi_tests

#endif // GEMPBA_TESTS_CABI_TEST_FIXTURE_HPP
