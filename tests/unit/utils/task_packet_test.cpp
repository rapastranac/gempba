/*
 * MIT License
 *
 * Copyright (c) 2025. Andrés Pastrana
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
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <gempba/utils/task_packet.hpp>


TEST(task_packet_test, construct_from_size) {
    gempba::task_packet v_packet(64);
    EXPECT_EQ(v_packet.size(), 64);
    std::memset(v_packet.data(), 0xAB, v_packet.size());

    for (std::size_t i = 0; i < v_packet.size(); ++i) {
        EXPECT_EQ(v_packet.data()[i], std::byte{0xAB});
    }
}

TEST(task_packet_test, construct_from_vector_copy) {
    std::vector v_bytes = {std::byte{1}, std::byte{2}, std::byte{3}};
    gempba::task_packet v_packet(v_bytes);

    EXPECT_EQ(v_packet.size(), v_bytes.size());
    EXPECT_NE(v_packet.data(), v_bytes.data()); // Should be a deep copy

    for (std::size_t i = 0; i < v_bytes.size(); ++i) {
        EXPECT_EQ(v_packet.data()[i], v_bytes[i]);
    }
}

TEST(task_packet_test, construct_from_vector_move) {
    std::vector v_bytes = {std::byte{10}, std::byte{20}};
    const auto *v_original_data_ptr = v_bytes.data();

    gempba::task_packet v_packet(std::move(v_bytes));

    EXPECT_EQ(v_packet.size(), 2);
    EXPECT_EQ(v_packet.data(), v_original_data_ptr);
    // Original vector should be empty
    EXPECT_TRUE(v_bytes.empty()); // NOLINT(bugprone-use-after-move)
}

TEST(task_packet_test, construct_from_cstring) {
    constexpr char v_raw[] = {'A', 'B', 'C'};
    gempba::task_packet v_packet(v_raw, sizeof(v_raw));

    EXPECT_EQ(v_packet.size(), sizeof(v_raw));
    EXPECT_EQ(std::memcmp(v_packet.data(), v_raw, sizeof(v_raw)), 0);
}

TEST(task_packet_test, construct_from_string) {
    const std::string v_string = "Hello, world!";
    gempba::task_packet v_packet(v_string);

    EXPECT_EQ(v_packet.size(), v_string.size());
    EXPECT_EQ(std::memcmp(v_packet.data(), v_string.data(), v_string.size()), 0);
}

TEST(task_packet_test, nullptr_with_non_zero_size_throws) { EXPECT_THROW(gempba::task_packet(nullptr, 10), std::invalid_argument); }

TEST(task_packet_test, copy_constructor_and_assignment) {
    const std::vector<std::byte> v_bytes = {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
    gempba::task_packet v_original(v_bytes);

    // Test copy constructor
    gempba::task_packet v_copy_constructed(v_original);
    EXPECT_EQ(v_copy_constructed.size(), v_original.size());
    EXPECT_NE(v_copy_constructed.data(), v_original.data()); // Deep copy
    for (std::size_t i = 0; i < v_original.size(); ++i) {
        EXPECT_EQ(v_copy_constructed.data()[i], v_original.data()[i]);
    }

    // Test copy assignment
    gempba::task_packet v_copy_assigned = gempba::task_packet(std::vector<std::byte>{std::byte{0x00}});
    v_copy_assigned = v_original;
    EXPECT_EQ(v_copy_assigned.size(), v_original.size());
    EXPECT_NE(v_copy_assigned.data(), v_original.data()); // Deep copy
    for (std::size_t i = 0; i < v_original.size(); ++i) {
        EXPECT_EQ(v_copy_assigned.data()[i], v_original.data()[i]);
    }
}


TEST(task_packet_test, move_semantics) {
    gempba::task_packet v_original(32);
    std::memset(v_original.data(), 0xCD, v_original.size());

    gempba::task_packet v_moved = std::move(v_original);

    EXPECT_EQ(v_moved.size(), 32);
    for (std::size_t i = 0; i < v_moved.size(); ++i) {
        EXPECT_EQ(v_moved.data()[i], std::byte{0xCD});
    }
}

TEST(task_packet_test, empty_query) {
    gempba::task_packet v_non_empty_packet(8);
    EXPECT_FALSE(v_non_empty_packet.empty());

    constexpr std::vector<std::byte> v_empty_vec;
    EXPECT_TRUE(v_empty_vec.empty());

    const gempba::task_packet v_moved_packet(std::move(v_non_empty_packet));
    EXPECT_EQ(8, v_moved_packet.size());
    EXPECT_FALSE(v_moved_packet.empty());

    // now the non-empty packet should be empty
    EXPECT_TRUE(v_non_empty_packet.empty()); // NOLINT(bugprone-use-after-move)
}


TEST(task_packet_test, const_iterator_yields_correct_bytes) {
    const std::vector v_expected_data = {std::byte{0x10}, std::byte{0x20}, std::byte{0x30}, std::byte{0x40}};

    const gempba::task_packet v_packet(v_expected_data);

    std::vector<std::byte> v_iterated_data;
    for (const std::byte &v_byte: v_packet) {
        v_iterated_data.push_back(v_byte);
    }

    EXPECT_EQ(v_iterated_data.size(), v_expected_data.size());
    EXPECT_EQ(v_iterated_data, v_expected_data);
}


TEST(task_packet_test, empty_packet_yields_nothing) {
    const gempba::task_packet &v_packet = gempba::task_packet::EMPTY;

    int v_count = 0;
    for ([[maybe_unused]] const std::byte &v_byte: v_packet) {
        ++v_count;
    }

    EXPECT_EQ(v_count, 0);
}


TEST(task_packet_test, basic_equality_and_inequality) {
    // Equal packets with same data
    const std::vector<std::byte> v_data1 = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const std::vector<std::byte> &v_data2 = v_data1; // copy

    gempba::task_packet v_packet1(v_data1);
    gempba::task_packet v_packet2(v_data2);

    EXPECT_TRUE(v_packet1 == v_packet2);
    EXPECT_FALSE(v_packet1 != v_packet2);

    // Reflexivity
    EXPECT_TRUE(v_packet1 == v_packet1);

    // Symmetry
    EXPECT_EQ(v_packet1 == v_packet2, v_packet2 == v_packet1);

    // Different size
    gempba::task_packet v_packet3(std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}});
    EXPECT_FALSE(v_packet1 == v_packet3);
    EXPECT_TRUE(v_packet1 != v_packet3);

    // Same size, different content
    std::vector<std::byte> v_data_diff = {std::byte{0x01}, std::byte{0xFF}, std::byte{0x03}};
    gempba::task_packet v_packet4(v_data_diff);
    EXPECT_FALSE(v_packet1 == v_packet4);
    EXPECT_TRUE(v_packet1 != v_packet4);

    // Empty packets equality
    gempba::task_packet v_manual_empty1(0);
    gempba::task_packet v_manual_empty2(std::vector<std::byte>{});
    EXPECT_TRUE(v_manual_empty1 == v_manual_empty2);
    EXPECT_FALSE(v_manual_empty1 != v_manual_empty2);
}


TEST(task_packet_test, empty_static_instance) {
    const gempba::task_packet &v_empty1 = gempba::task_packet::EMPTY;
    const gempba::task_packet v_manual_empty(0);

    // EMPTY should be equal to manually created empty packets
    EXPECT_TRUE(v_empty1 == v_manual_empty);
    EXPECT_TRUE(v_manual_empty == v_empty1);
    EXPECT_FALSE(v_empty1 != v_manual_empty);
    EXPECT_FALSE(v_manual_empty != v_empty1);

    // EMPTY should be equal to itself
    EXPECT_TRUE(v_empty1 == v_empty1);
}

TEST(task_packet_test, spaceship_operator_comparison_and_set_uniqueness) {
    const gempba::task_packet v_packet1("hello", 5);
    const gempba::task_packet v_packet2("hello", 5);
    const gempba::task_packet v_packet3("world", 5);
    const gempba::task_packet v_packet4("hi", 2);
    const gempba::task_packet v_packet5("hello world", 11);

    // Test equality
    EXPECT_TRUE(v_packet1 == v_packet2);
    EXPECT_FALSE(v_packet1 == v_packet3);
    EXPECT_FALSE(v_packet1 != v_packet2);
    EXPECT_TRUE(v_packet1 != v_packet3);

    // Test ordering (size first, then lexicographic)
    EXPECT_TRUE(v_packet4 < v_packet1); // "hi" (2) < "hello" (5) by size
    EXPECT_TRUE(v_packet1 < v_packet5); // "hello" (5) < "hello world" (11) by size
    EXPECT_TRUE(v_packet1 < v_packet3); // "hello" < "world" lexicographically (same size)
    EXPECT_FALSE(v_packet3 < v_packet1); // "world" > "hello"

    // Test set uniqueness - no collisions should occur
    std::set<gempba::task_packet> v_packet_set;

    const auto [v_iter1, v_inserted1] = v_packet_set.insert(v_packet1);
    EXPECT_TRUE(v_inserted1);

    const auto [v_iter2, v_inserted2] = v_packet_set.insert(v_packet2);
    EXPECT_FALSE(v_inserted2); // Should not insert duplicate

    const auto [v_iter3, v_inserted3] = v_packet_set.insert(v_packet3);
    EXPECT_TRUE(v_inserted3);

    const auto [v_iter4, v_inserted4] = v_packet_set.insert(v_packet4);
    EXPECT_TRUE(v_inserted4);

    const auto [v_iter5, v_inserted5] = v_packet_set.insert(v_packet5);
    EXPECT_TRUE(v_inserted5);

    EXPECT_EQ(v_packet_set.size(), 4u); // v_packet1 and v_packet2 are identical

    // Verify ordering in set (should be: v_packet4, v_packet1, v_packet3, v_packet5)
    auto v_it = v_packet_set.begin();
    EXPECT_TRUE(*v_it == v_packet4); // smallest size (2)
    ++v_it;
    EXPECT_TRUE(*v_it == v_packet1); // size 5, "hello"
    ++v_it;
    EXPECT_TRUE(*v_it == v_packet3); // size 5, "world"
    ++v_it;
    EXPECT_TRUE(*v_it == v_packet5); // largest size (11)
}
