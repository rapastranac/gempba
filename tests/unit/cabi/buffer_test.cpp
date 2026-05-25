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
 * Tests for gempba_buffer_* — the owned-buffer allocator/freer that
 * bindings use to receive bytes produced by gempba (runnable results,
 * lazy-args producers, node_manager::get_result, …). The contract under
 * test is documented in include/gempba/cabi/gempba.h: zero-length alloc returns
 * {NULL, 0}; free zeroes the handle.
 */

#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_buffer_test : public cabi_fixture {};

    TEST_F(cabi_buffer_test, buffer_alloc_zero_returns_empty) {
        gempba_buffer_t v_buf = gempba_buffer_alloc(0);
        EXPECT_EQ(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, 0u);
        gempba_buffer_free(&v_buf); // safe on {NULL, 0}
    }

    TEST_F(cabi_buffer_test, buffer_alloc_then_free_resets_handle) {
        gempba_buffer_t v_buf = gempba_buffer_alloc(32);
        ASSERT_NE(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, 32u);
        std::memset(v_buf.data, 0xAB, v_buf.len);
        gempba_buffer_free(&v_buf);
        EXPECT_EQ(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, 0u);
    }

} // namespace gempba::cabi_tests
