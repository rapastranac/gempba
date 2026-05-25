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
 * Tests for the gempba_*_create_load_balancer factories. The C++ load
 * balancer is a singleton owned by gempba — once one exists the factory
 * must refuse another, and gempba_get_load_balancer must hand back exactly
 * the same pointer the factory returned.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_load_balancer_test : public cabi_fixture {};

    TEST_F(cabi_load_balancer_test, mt_create_returns_same_as_singleton) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        EXPECT_EQ(v_lb, gempba_get_load_balancer());
    }

    TEST_F(cabi_load_balancer_test, mt_create_twice_returns_null_with_error) {
        gempba_load_balancer_t v_first = gempba_mt_create_load_balancer(GEMPBA_BALANCING_QUASI_HORIZONTAL);
        ASSERT_NE(v_first, nullptr);

        gempba_load_balancer_t v_second = gempba_mt_create_load_balancer(GEMPBA_BALANCING_QUASI_HORIZONTAL);
        EXPECT_EQ(v_second, nullptr);
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }

} // namespace gempba::cabi_tests
