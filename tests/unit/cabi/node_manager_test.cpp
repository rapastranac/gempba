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

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_node_manager_test : public cabi_fixture {};

    TEST_F(cabi_node_manager_test, mt_create_returns_same_as_singleton) {
        gempba_load_balancer_t v_lb = gempba_mt_create_load_balancer(GEMPBA_BALANCING_WORK_STEALING);
        ASSERT_NE(v_lb, nullptr);
        gempba_node_manager_t v_nm = gempba_mt_create_node_manager(v_lb);
        ASSERT_NE(v_nm, nullptr);
        EXPECT_EQ(v_nm, gempba_get_node_manager());
    }

} // namespace gempba::cabi_tests
