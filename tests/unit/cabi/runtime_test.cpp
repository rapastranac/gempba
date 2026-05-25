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
 * Tests for the process-wide runtime plumbing: gempba_last_error_message,
 * gempba_clear_last_error, and the "no singletons before any factory ran"
 * baseline. Anything else that lives below the load balancer / node
 * manager / scheduler surfaces moves to its own file.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_runtime_test : public cabi_fixture {};

    TEST_F(cabi_runtime_test, last_error_initially_clear) {
        gempba_clear_last_error();
        EXPECT_EQ(gempba_last_error_message(), nullptr);
    }

} // namespace gempba::cabi_tests
