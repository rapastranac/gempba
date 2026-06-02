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
 * Tests for the gempba_telemetry_* process-wide flag.  The fixture restores
 * the flag in SetUp / TearDown, so each test starts from the documented
 * default (enabled) regardless of test ordering.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_telemetry_test : public cabi_fixture {};

    TEST_F(cabi_telemetry_test, is_enabled_by_default) {
        // The fixture restores the flag in SetUp, so this is the documented
        // default state observed at the start of any test.
        EXPECT_EQ(gempba_telemetry_is_enabled(), 1);
    }

    TEST_F(cabi_telemetry_test, disable_clears_flag) {
        gempba_telemetry_disable();
        EXPECT_EQ(gempba_telemetry_is_enabled(), 0);
    }

    TEST_F(cabi_telemetry_test, enable_after_disable_restores_flag) {
        gempba_telemetry_disable();
        ASSERT_EQ(gempba_telemetry_is_enabled(), 0);
        gempba_telemetry_enable();
        EXPECT_EQ(gempba_telemetry_is_enabled(), 1);
    }

    TEST_F(cabi_telemetry_test, enable_is_idempotent) {
        gempba_telemetry_enable();
        gempba_telemetry_enable();
        EXPECT_EQ(gempba_telemetry_is_enabled(), 1);
    }

    TEST_F(cabi_telemetry_test, disable_is_idempotent) {
        gempba_telemetry_disable();
        gempba_telemetry_disable();
        EXPECT_EQ(gempba_telemetry_is_enabled(), 0);
    }

    // configure_port is write-only (the bound port is not observable through the
    // C ABI), so this only asserts the call is safe before any hub install and
    // leaves the enabled flag untouched.
    TEST_F(cabi_telemetry_test, configure_port_is_callable_and_independent_of_flag) {
        ASSERT_EQ(gempba_telemetry_is_enabled(), 1);
        gempba_telemetry_configure_port(9100);
        gempba_telemetry_configure_port(0);
        EXPECT_EQ(gempba_telemetry_is_enabled(), 1);
    }

} // namespace gempba::cabi_tests
