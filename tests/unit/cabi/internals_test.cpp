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
 * Direct unit tests for the C ABI's internal helpers (private/cabi/internals.hpp).
 * The cabi_*_test files reach these helpers only transitively through end-to-end
 * runnable execution; these tests pin their behaviour in isolation so a
 * regression in (say) packet_from_owned surfaces here instead of via a
 * far-removed scheduler test failure.
 */

#include <atomic>
#include <cstring>

#include <cabi/internals.hpp>
#include <cabi_test_fixture.hpp>

namespace gempba::cabi_tests {

    class cabi_internals_test : public cabi_fixture {};

    using gempba::cabi::detail::buffer_from_packet;
    using gempba::cabi::detail::make_holder;
    using gempba::cabi::detail::packet_from_borrowed;
    using gempba::cabi::detail::packet_from_owned;
    using gempba::cabi::detail::safe_make_holder;
    using gempba::cabi::detail::set_last_error;

    // ─── packet_from_borrowed ───────────────────────────────────────────────

    TEST_F(cabi_internals_test, packet_from_borrowed_returns_empty_on_null_bytes) {
        const auto v_pkt = packet_from_borrowed(gempba_bytes_t{nullptr, 0});
        EXPECT_TRUE(v_pkt.empty());
    }

    TEST_F(cabi_internals_test, packet_from_borrowed_returns_empty_on_zero_len_with_data) {
        const uint8_t v_buf[] = {0xAA};
        const auto v_pkt = packet_from_borrowed(gempba_bytes_t{v_buf, 0});
        EXPECT_TRUE(v_pkt.empty());
    }

    TEST_F(cabi_internals_test, packet_from_borrowed_copies_bytes) {
        const uint8_t v_buf[] = {0xDE, 0xAD, 0xBE, 0xEF};
        const auto v_pkt = packet_from_borrowed(gempba_bytes_t{v_buf, sizeof(v_buf)});
        ASSERT_EQ(v_pkt.size(), sizeof(v_buf));
        EXPECT_EQ(std::memcmp(v_pkt.data(), v_buf, sizeof(v_buf)), 0);
    }

    // ─── packet_from_owned ──────────────────────────────────────────────────

    TEST_F(cabi_internals_test, packet_from_owned_returns_empty_on_null_buf_pointer) {
        const auto v_pkt = packet_from_owned(nullptr);
        EXPECT_TRUE(v_pkt.empty());
    }

    TEST_F(cabi_internals_test, packet_from_owned_consumes_and_zeros_input_buffer) {
        gempba_buffer_t v_buf = gempba_buffer_alloc(3);
        ASSERT_NE(v_buf.data, nullptr);
        v_buf.data[0] = 0xC0;
        v_buf.data[1] = 0xFF;
        v_buf.data[2] = 0xEE;

        const auto v_pkt = packet_from_owned(&v_buf);

        // Input is consumed: buffer_free zeros the handle.
        EXPECT_EQ(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, 0u);
        // Packet carries the original payload.
        ASSERT_EQ(v_pkt.size(), 3u);
        EXPECT_EQ(v_pkt.data()[0], std::byte{0xC0});
        EXPECT_EQ(v_pkt.data()[1], std::byte{0xFF});
        EXPECT_EQ(v_pkt.data()[2], std::byte{0xEE});
    }

    TEST_F(cabi_internals_test, packet_from_owned_frees_empty_buf_and_returns_empty) {
        gempba_buffer_t v_buf{nullptr, 0};
        const auto v_pkt = packet_from_owned(&v_buf);
        EXPECT_TRUE(v_pkt.empty());
        EXPECT_EQ(v_buf.data, nullptr);
    }

    // ─── buffer_from_packet ─────────────────────────────────────────────────

    TEST_F(cabi_internals_test, buffer_from_packet_returns_empty_on_empty_packet) {
        const auto v_buf = buffer_from_packet(gempba::task_packet::EMPTY);
        EXPECT_EQ(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, 0u);
    }

    TEST_F(cabi_internals_test, buffer_from_packet_round_trips_bytes) {
        const uint8_t v_payload[] = {0x12, 0x34, 0x56};
        const auto v_pkt = packet_from_borrowed(gempba_bytes_t{v_payload, sizeof(v_payload)});

        auto v_buf = buffer_from_packet(v_pkt);
        ASSERT_NE(v_buf.data, nullptr);
        EXPECT_EQ(v_buf.len, sizeof(v_payload));
        EXPECT_EQ(std::memcmp(v_buf.data, v_payload, sizeof(v_payload)), 0);
        gempba_buffer_free(&v_buf);
    }

    // ─── make_holder / safe_make_holder release contract ────────────────────

    namespace {
        std::atomic<int> g_release_count{0};
        void release_inc(void* p_data) {
            EXPECT_EQ(p_data, reinterpret_cast<void*>(0xDEAD));
            g_release_count.fetch_add(1, std::memory_order_relaxed);
        }
    } // namespace

    TEST_F(cabi_internals_test, make_holder_release_fires_when_last_copy_drops) {
        g_release_count.store(0);
        {
            auto v_h = make_holder(reinterpret_cast<void*>(0xDEAD), release_inc);
            auto v_copy = v_h; // NOLINT(performance-unnecessary-copy-initialization) — bumps refcount → 2; clang-tidy can't see the side effect
            EXPECT_EQ(g_release_count.load(), 0);
        }
        EXPECT_EQ(g_release_count.load(), 1);
    }

    TEST_F(cabi_internals_test, make_holder_skips_release_when_data_is_null) {
        g_release_count.store(0);
        {
            auto v_h = make_holder(nullptr, release_inc);
        }
        EXPECT_EQ(g_release_count.load(), 0);
    }

    TEST_F(cabi_internals_test, safe_make_holder_returns_a_holder_on_success) {
        g_release_count.store(0);
        {
            auto v_h = safe_make_holder(reinterpret_cast<void*>(0xDEAD), release_inc);
            ASSERT_NE(v_h, nullptr);
        }
        EXPECT_EQ(g_release_count.load(), 1);
    }

    // ─── set_last_error / g_last_error wiring through the C ABI ─────────────

    TEST_F(cabi_internals_test, set_last_error_is_visible_via_c_abi_message) {
        set_last_error("internals-set");
        EXPECT_STREQ(gempba_last_error_message(), "internals-set");
        gempba_clear_last_error();
        EXPECT_EQ(gempba_last_error_message(), nullptr);
    }

    TEST_F(cabi_internals_test, set_last_error_with_null_clears) {
        set_last_error("foo");
        set_last_error(nullptr);
        EXPECT_EQ(gempba_last_error_message(), nullptr);
    }

} // namespace gempba::cabi_tests
