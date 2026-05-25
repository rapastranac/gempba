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
 * Tests for the gempba_scheduler_* surface.  Only the no-MPI-runtime-needed
 * branches are covered here; full scheduler happy paths require a live MPI
 * process group and live in MPI-launched integration tests, not the unit
 * suite.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_scheduler_test : public cabi_fixture {};

    TEST_F(cabi_scheduler_test, create_null_out_returns_invalid_arg) {
        EXPECT_EQ(gempba_scheduler_create(GEMPBA_TOPOLOGY_CENTRALIZED, /*timeout=*/0.0, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }

    TEST_F(cabi_scheduler_test, destroy_null_is_safe) {
        gempba_scheduler_destroy(nullptr);
        SUCCEED();
    }

} // namespace gempba::cabi_tests
