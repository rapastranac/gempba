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
 */
/**
 * test_centralized_utils.cpp
 *
 * GTest suite for centralized_utils.hpp — 100% branch and line coverage.
 * Compiles and runs on all platforms supported by the header:
 *   Windows (MSYS2/MinGW, MSVC), Linux, macOS, AIX, Solaris.
 *
 * Build (MSYS2/MinGW — your primary env):
 *   g++ -std=c++23 -O0 --coverage \
 *       test_centralized_utils.cpp -o test_centralized_utils \
 *       -lgtest -lpthread -lpsapi
 *
 * Build (Linux/macOS):
 *   g++ -std=c++23 -O0 --coverage \
 *       test_centralized_utils.cpp -o test_centralized_utils \
 *       -lgtest -lpthread
 */

// -- Windows include-order fix ------------------------------------------------
// centralized_utils.hpp defines NOMINMAX / WIN32_LEAN_AND_MEAN and then pulls
// in <psapi.h> BEFORE <windows.h>.  On MSYS2/MinGW, psapi.h depends on Win32
// types (WINBOOL, DWORD, SIZE_T, ULONG_PTR ...) that are only defined after
// windows.h has been processed, causing ~100 errors.  Pre-including the Win32
// headers here — with the same guards the production header uses — establishes
// the correct order before centralized_utils.hpp is seen by the preprocessor.
#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef RPC_NO_WINDOWS_H
        #define RPC_NO_WINDOWS_H
    #endif
    // psapi.h depends on Win32 types defined by windows.h, so order is significant.
    // clang-format off
    #include <windows.h>
    #include <psapi.h>
// clang-format on
#endif
// -----------------------------------------------------------------------------

#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>
#include <impl/schedulers/centralized_utils.hpp>

#include <algorithm>
#include <cstddef>
#include <gtest/gtest.h>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

    /** Build a task_packet from a list of raw unsigned byte values. */
    gempba::task_packet make_packet(std::initializer_list<unsigned char> p_bytes) {
        std::vector<std::byte> v;
        v.reserve(p_bytes.size());
        for (auto b: p_bytes)
            v.push_back(static_cast<std::byte>(b));
        return gempba::task_packet{std::move(v)};
    }

    /** Reference popcount — cross-validates the production octal-arithmetic formula. */
    int naive_popcount(unsigned char p_v) {
        int n = 0;
        while (p_v) {
            n += (p_v & 1);
            p_v >>= 1u;
        }
        return n;
    }

    /** Expected bit-count for a packet, computed via reference popcount. */
    int expected_packet_bits(std::initializer_list<unsigned char> p_bytes) {
        int n = 0;
        for (auto b: p_bytes)
            n += naive_popcount(b);
        return n;
    }

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. get_nb_set_bits(char)
// ═════════════════════════════════════════════════════════════════════════════

class get_nb_set_bits_char_test : public ::testing::Test {};

TEST_F(get_nb_set_bits_char_test, zero_byte_returns_zero) { EXPECT_EQ(0, get_nb_set_bits(static_cast<char>(0x00))); }

TEST_F(get_nb_set_bits_char_test, all_ones_byte_returns_eight) { EXPECT_EQ(8, get_nb_set_bits(static_cast<char>(0xFF))); }

TEST_F(get_nb_set_bits_char_test, single_bit_each_position_returns_one) {
    for (int v_shift = 0; v_shift < 8; ++v_shift) {
        char c = static_cast<char>(1u << v_shift);
        EXPECT_EQ(1, get_nb_set_bits(c)) << "bit position " << v_shift;
    }
}

TEST_F(get_nb_set_bits_char_test, all_256_values_match_naive_popcount) {
    for (int i = 0; i < 256; ++i) {
        auto v_uc = static_cast<unsigned char>(i);
        EXPECT_EQ(naive_popcount(v_uc), get_nb_set_bits(static_cast<char>(v_uc))) << "byte value " << i;
    }
}

TEST_F(get_nb_set_bits_char_test, alternating_patterns_return_four) {
    EXPECT_EQ(4, get_nb_set_bits(static_cast<char>(0xAA))); // 10101010
    EXPECT_EQ(4, get_nb_set_bits(static_cast<char>(0x55))); // 01010101
    EXPECT_EQ(4, get_nb_set_bits(static_cast<char>(0x0F))); // 00001111
    EXPECT_EQ(4, get_nb_set_bits(static_cast<char>(0xF0))); // 11110000
}

TEST_F(get_nb_set_bits_char_test, known_asymmetric_patterns) {
    EXPECT_EQ(7, get_nb_set_bits(static_cast<char>(0xFE))); // 11111110
    EXPECT_EQ(7, get_nb_set_bits(static_cast<char>(0x7F))); // 01111111
    EXPECT_EQ(2, get_nb_set_bits(static_cast<char>(0x81))); // 10000001
    EXPECT_EQ(3, get_nb_set_bits(static_cast<char>(0x07))); // 00000111
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. get_nb_set_bits(task_packet)
// ═════════════════════════════════════════════════════════════════════════════

class get_nb_set_bits_packet_test : public ::testing::Test {};

TEST_F(get_nb_set_bits_packet_test, empty_singleton_returns_zero) { EXPECT_EQ(0, get_nb_set_bits(gempba::task_packet::EMPTY)); }

TEST_F(get_nb_set_bits_packet_test, size_only_constructed_zero_initialized_returns_zero) {
    // task_packet(size_t) — bytes are value-initialized to 0
    gempba::task_packet v_pkt{std::size_t{4}};
    EXPECT_EQ(0, get_nb_set_bits(v_pkt));
}

TEST_F(get_nb_set_bits_packet_test, single_all_zeros_byte_returns_zero) { EXPECT_EQ(0, get_nb_set_bits(make_packet({0x00}))); }

TEST_F(get_nb_set_bits_packet_test, single_all_ones_byte_returns_eight) {
    auto b = get_nb_set_bits(make_packet({0xFF}));
    EXPECT_EQ(8, b);
}

TEST_F(get_nb_set_bits_packet_test, multi_byte_sum_is_correct) {
    // 0x0F(4) + 0xF0(4) + 0xFF(8) = 16
    EXPECT_EQ(16, get_nb_set_bits(make_packet({0x0F, 0xF0, 0xFF})));
}

TEST_F(get_nb_set_bits_packet_test, zero_padding_does_not_change_sum) { EXPECT_EQ(get_nb_set_bits(make_packet({0x0F})), get_nb_set_bits(make_packet({0x00, 0x0F, 0x00}))); }

TEST_F(get_nb_set_bits_packet_test, mixed_bytes_match_naive_sum) {
    std::initializer_list<unsigned char> v_raw = {0xA1, 0x3C, 0x7E, 0x00, 0xFF};
    EXPECT_EQ(expected_packet_bits(v_raw), get_nb_set_bits(make_packet(v_raw)));
}

TEST_F(get_nb_set_bits_packet_test, constructed_from_string_matches_char_sum) {
    // "AB" -> 0x41(A), 0x42(B)
    std::string s{"AB"};
    gempba::task_packet v_pkt{s};
    EXPECT_EQ(naive_popcount(0x41) + naive_popcount(0x42), get_nb_set_bits(v_pkt));
}

TEST_F(get_nb_set_bits_packet_test, constructed_from_char_buffer_matches_expected) {
    const char v_buf[] = {'\x0F', '\xFF'};
    gempba::task_packet v_pkt{v_buf, sizeof(v_buf)};
    EXPECT_EQ(naive_popcount(0x0F) + naive_popcount(0xFF), get_nb_set_bits(v_pkt));
}

TEST_F(get_nb_set_bits_packet_test, large_packet_matches_naive_sum) {
    constexpr int v_n = 1024;
    std::vector<std::byte> v_bytes;
    v_bytes.reserve(v_n);
    int v_expected = 0;
    for (int i = 0; i < v_n; ++i) {
        auto v_uc = static_cast<unsigned char>(i % 256);
        v_bytes.push_back(static_cast<std::byte>(v_uc));
        v_expected += naive_popcount(v_uc);
    }
    gempba::task_packet v_pkt{std::move(v_bytes)};
    EXPECT_EQ(v_expected, get_nb_set_bits(v_pkt));
}

TEST_F(get_nb_set_bits_packet_test, copy_constructed_packet_gives_same_result) {
    auto v_original = make_packet({0xAA, 0x55});
    const gempba::task_packet &v_copy{v_original};
    EXPECT_EQ(get_nb_set_bits(v_original), get_nb_set_bits(v_copy));
}

TEST_F(get_nb_set_bits_packet_test, move_constructed_packet_gives_same_result) {
    auto v_pkt = make_packet({0xAA, 0x55});
    const int v_before = get_nb_set_bits(v_pkt);
    gempba::task_packet v_moved{std::move(v_pkt)};
    EXPECT_EQ(v_before, get_nb_set_bits(v_moved));
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. task_comparator
// ═════════════════════════════════════════════════════════════════════════════

class task_comparator_test : public ::testing::Test {
protected:
    task_comparator m_cmp;
};

TEST_F(task_comparator_test, both_empty_returns_true) { EXPECT_TRUE(m_cmp(gempba::task_packet::EMPTY, gempba::task_packet::EMPTY)); }

TEST_F(task_comparator_test, equal_bit_count_returns_true) {
    // 0xAA and 0x0F both have 4 bits
    EXPECT_TRUE(m_cmp(make_packet({0xAA}), make_packet({0x0F})));
}

TEST_F(task_comparator_test, fewer_bits_on_left_returns_true) { EXPECT_TRUE(m_cmp(make_packet({0x01}), make_packet({0xFF}))); }

TEST_F(task_comparator_test, more_bits_on_left_returns_false) { EXPECT_FALSE(m_cmp(make_packet({0xFF}), make_packet({0x01}))); }

TEST_F(task_comparator_test, reflexive_same_packet_returns_true) {
    auto v_pkt = make_packet({0x3C});
    EXPECT_TRUE(m_cmp(v_pkt, v_pkt));
}

TEST_F(task_comparator_test, transitive_ordering_is_consistent) {
    auto v_p_low = make_packet({0x01}); // 1 bit
    auto v_p_mid = make_packet({0x0F}); // 4 bits
    auto v_p_high = make_packet({0xFF}); // 8 bits
    EXPECT_TRUE(m_cmp(v_p_low, v_p_mid));
    EXPECT_TRUE(m_cmp(v_p_mid, v_p_high));
    EXPECT_TRUE(m_cmp(v_p_low, v_p_high));
}

TEST_F(task_comparator_test, multi_byte_vs_single_byte_uses_total_bits) {
    // {0x01, 0x01} = 2 bits vs {0xFF} = 8 bits
    EXPECT_TRUE(m_cmp(make_packet({0x01, 0x01}), make_packet({0xFF})));
    EXPECT_FALSE(m_cmp(make_packet({0xFF}), make_packet({0x01, 0x01})));
}

TEST_F(task_comparator_test, usable_in_std_sort_produces_ascending_order) {
    std::vector<gempba::task_packet> v_pkts;
    v_pkts.push_back(make_packet({0xFF})); // 8 bits
    v_pkts.push_back(make_packet({0x01})); // 1 bit
    v_pkts.push_back(make_packet({0x0F})); // 4 bits

    std::ranges::sort(v_pkts, [](const auto &p_a, const auto &p_b) { return get_nb_set_bits(p_a) < get_nb_set_bits(p_b); });

    EXPECT_LE(get_nb_set_bits(v_pkts[0]), get_nb_set_bits(v_pkts[1]));
    EXPECT_LE(get_nb_set_bits(v_pkts[1]), get_nb_set_bits(v_pkts[2]));
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. get_nb_set_bits(task_bundle)
// ═════════════════════════════════════════════════════════════════════════════

class get_nb_set_bits_bundle_test : public ::testing::Test {};

TEST_F(get_nb_set_bits_bundle_test, zero_packet_and_zero_id_returns_zero) {
    gempba::task_bundle b{make_packet({0x00}), 0};
    EXPECT_EQ(0, get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, zero_packet_nonzero_id_returns_id_bits_only) {
    // id = 7 = 0b0000'0111 -> 3 set bits
    gempba::task_bundle b{make_packet({0x00}), 7};
    EXPECT_EQ(3, get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, nonzero_packet_zero_id_returns_packet_bits_only) {
    gempba::task_bundle b{make_packet({0xFF}), 0};
    EXPECT_EQ(8, get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, both_nonzero_returns_combined_bits) {
    // 0x0F(4) + id=0x0F cast to char(4) = 8
    gempba::task_bundle b{make_packet({0x0F}), 0x0F};
    EXPECT_EQ(8, get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, empty_packet_singleton_only_id_bits_counted) {
    gempba::task_bundle b{gempba::task_packet::EMPTY, 0xFF};
    EXPECT_EQ(get_nb_set_bits(static_cast<char>(0xFF)), get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, multi_byte_packet_combined_with_id) {
    // 0xAA(4) + 0x55(4) = 8, id = 3 = 0b11 -> 2 bits -> total 10
    gempba::task_bundle b{make_packet({0xAA, 0x55}), 3};
    EXPECT_EQ(10, get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, id_is_cast_to_char_using_lower_8_bits) {
    // Verifies static_cast<char>(runnable_id) behaviour for edge-case ids
    for (int v_id: {0, 1, 127, 128, 255, 256, -1, -128}) {
        gempba::task_bundle b{make_packet({0x00}), v_id};
        EXPECT_EQ(get_nb_set_bits(static_cast<char>(v_id)), get_nb_set_bits(b)) << "id = " << v_id;
    }
}

TEST_F(get_nb_set_bits_bundle_test, result_equals_packet_bits_plus_id_bits) {
    auto v_pkt = make_packet({0xA1, 0x3C});
    const int v_id = 42;
    gempba::task_bundle b{v_pkt, v_id};
    EXPECT_EQ(get_nb_set_bits(v_pkt) + get_nb_set_bits(static_cast<char>(v_id)), get_nb_set_bits(b));
}

TEST_F(get_nb_set_bits_bundle_test, get_task_packet_returns_by_value_not_dangling) {
    // get_task_packet() returns by value; the copy must give the same bit count.
    auto v_pkt = make_packet({0xDE, 0xAD});
    gempba::task_bundle b{v_pkt, 0};
    EXPECT_EQ(get_nb_set_bits(v_pkt), get_nb_set_bits(b.get_task_packet()));
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. task_bundle_comparator
// ═════════════════════════════════════════════════════════════════════════════

class task_bundle_comparator_test : public ::testing::Test {
protected:
    task_bundle_comparator m_cmp;
};

TEST_F(task_bundle_comparator_test, both_zero_bits_returns_true) {
    gempba::task_bundle v_b1{make_packet({0x00}), 0};
    gempba::task_bundle v_b2{make_packet({0x00}), 0};
    EXPECT_TRUE(m_cmp(v_b1, v_b2));
}

TEST_F(task_bundle_comparator_test, equal_bit_count_returns_true) {
    gempba::task_bundle v_b1{make_packet({0x0F}), 0}; // 4 bits
    gempba::task_bundle v_b2{make_packet({0xAA}), 0}; // 4 bits
    EXPECT_TRUE(m_cmp(v_b1, v_b2));
}

TEST_F(task_bundle_comparator_test, fewer_bits_on_left_returns_true) {
    gempba::task_bundle v_b_few{make_packet({0x01}), 0}; // 1 bit
    gempba::task_bundle v_b_many{make_packet({0xFF}), 0xFF}; // 16 bits
    EXPECT_TRUE(m_cmp(v_b_few, v_b_many));
}

TEST_F(task_bundle_comparator_test, more_bits_on_left_returns_false) {
    gempba::task_bundle v_b_many{make_packet({0xFF}), 0xFF}; // 16 bits
    gempba::task_bundle v_b_few{make_packet({0x01}), 0}; // 1 bit
    EXPECT_FALSE(m_cmp(v_b_many, v_b_few));
}

TEST_F(task_bundle_comparator_test, reflexive_same_bundle_returns_true) {
    gempba::task_bundle b{make_packet({0xAA}), 5};
    EXPECT_TRUE(m_cmp(b, b));
}

TEST_F(task_bundle_comparator_test, runnable_id_alone_shifts_outcome) {
    gempba::task_bundle v_b1{make_packet({0x01}), 0}; // 1 + 0 = 1 bit
    gempba::task_bundle v_b2{make_packet({0x01}), 3}; // 1 + 2 = 3 bits
    EXPECT_TRUE(m_cmp(v_b1, v_b2));
    EXPECT_FALSE(m_cmp(v_b2, v_b1));
}

TEST_F(task_bundle_comparator_test, transitive_ordering_is_consistent) {
    gempba::task_bundle v_b_low{make_packet({0x01}), 0}; // 1 bit
    gempba::task_bundle v_b_mid{make_packet({0x0F}), 0}; // 4 bits
    gempba::task_bundle v_b_high{make_packet({0xFF}), 0}; // 8 bits
    EXPECT_TRUE(m_cmp(v_b_low, v_b_mid));
    EXPECT_TRUE(m_cmp(v_b_mid, v_b_high));
    EXPECT_TRUE(m_cmp(v_b_low, v_b_high));
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. get_peak_rss / get_current_rss — portable smoke tests
//
//    Implementations are #if-gated per platform. Tests mirror those guards so
//    only the branch that actually compiled is exercised. On platforms where
//    both functions return 0 (unknown OS), assertions still pass.
// ═════════════════════════════════════════════════════════════════════════════

class get_peak_rss_test : public ::testing::Test {};

TEST_F(get_peak_rss_test, returns_non_negative) { EXPECT_GE(get_peak_rss(), static_cast<std::size_t>(0)); }

TEST_F(get_peak_rss_test, is_monotonically_non_decreasing_after_allocation) {
    const std::size_t v_before = get_peak_rss();
    {
        // Touch every page so the OS actually maps the memory
        volatile std::vector<char> v_big(4u * 1024u * 1024u, 'x');
        (void) v_big;
    }
    EXPECT_GE(get_peak_rss(), v_before);
}

TEST_F(get_peak_rss_test, stable_across_repeated_calls) {
    std::size_t v_prev = get_peak_rss();
    for (int i = 0; i < 32; ++i) {
        std::size_t v_cur = get_peak_rss();
        EXPECT_GE(v_cur, v_prev);
        v_prev = v_cur;
    }
}

class get_current_rss_test : public ::testing::Test {};

TEST_F(get_current_rss_test, returns_non_negative) { EXPECT_GE(get_current_rss(), static_cast<std::size_t>(0)); }

#if defined(__linux__) || defined(__linux) || defined(linux) || defined(__gnu_linux__)
TEST_F(get_current_rss_test, linux_result_is_page_aligned) {
    const std::size_t v_current = get_current_rss();
    const long v_page_size = sysconf(_SC_PAGESIZE);
    if (v_current > 0 && v_page_size > 0) {
        EXPECT_EQ(0u, v_current % static_cast<std::size_t>(v_page_size));
    }
}
#endif

TEST_F(get_current_rss_test, stable_over_many_calls_without_crash) {
    for (int i = 0; i < 64; ++i) {
        EXPECT_GE(get_current_rss(), static_cast<std::size_t>(0));
    }
}
