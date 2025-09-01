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
#include <vector>
#include <gtest/gtest.h>

#include <utils/ipc/task_bundle.hpp>
#include <utils/ipc/task_packet.hpp>

TEST(task_bundle_test, construct_with_task_packet_and_function_id) {
    const std::vector v_data = {std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    const gempba::task_packet v_packet(v_data);
    constexpr int v_function_id = 42;

    const gempba::task_bundle v_bundle(v_packet, v_function_id);

    EXPECT_EQ(v_bundle.get_runnable_id(), v_function_id);
    EXPECT_EQ(v_bundle.get_task_packet().size(), v_data.size());
    EXPECT_EQ(v_bundle.size(), v_data.size());
    EXPECT_FALSE(v_bundle.empty());
}

TEST(task_bundle_test, construct_with_move_semantics) {
    std::vector v_data = {std::byte{0xAA}, std::byte{0xBB}};
    gempba::task_packet v_packet(std::move(v_data));
    constexpr int v_function_id = 123;

    const gempba::task_bundle v_bundle(std::move(v_packet), v_function_id);

    EXPECT_EQ(v_bundle.get_runnable_id(), v_function_id);
    EXPECT_EQ(v_bundle.size(), 2);
    EXPECT_FALSE(v_bundle.empty());
}

TEST(task_bundle_test, construct_with_empty_packet) {
    const gempba::task_packet v_empty_packet(0);
    constexpr int v_function_id = 999;

    const gempba::task_bundle v_bundle(v_empty_packet, v_function_id);

    EXPECT_EQ(v_bundle.get_runnable_id(), v_function_id);
    EXPECT_EQ(v_bundle.size(), 0);
    EXPECT_TRUE(v_bundle.empty());
}

TEST(task_bundle_test, construct_with_static_empty_packet) {
    constexpr int v_function_id = 0;

    const gempba::task_bundle v_bundle(gempba::task_packet::EMPTY, v_function_id);

    EXPECT_EQ(v_bundle.get_runnable_id(), v_function_id);
    EXPECT_EQ(v_bundle.size(), 0);
    EXPECT_TRUE(v_bundle.empty());
}

TEST(task_bundle_test, data_getter_returns_const_reference) {
    const std::vector v_expected = {std::byte{0xFF}, std::byte{0x00}, std::byte{0xFF}};
    const gempba::task_packet v_packet(v_expected);
    const gempba::task_bundle v_bundle(v_packet, 456);

    const gempba::task_packet &v_data_ref = v_bundle.get_task_packet();

    EXPECT_EQ(v_data_ref.size(), v_expected.size());
    for (std::size_t i = 0; i < v_expected.size(); ++i) {
        EXPECT_EQ(v_data_ref.data()[i], v_expected[i]);
    }
}

TEST(task_bundle_test, immutability_const_members) {
    const gempba::task_packet v_packet(std::vector<std::byte>{std::byte{0x12}});
    const gempba::task_bundle v_bundle(v_packet, 789);

    // This should compile - getters should work on const bundle
    const gempba::task_bundle &v_const_ref = v_bundle;
    EXPECT_EQ(v_const_ref.get_runnable_id(), 789);
    EXPECT_EQ(v_const_ref.size(), 1);
    EXPECT_FALSE(v_const_ref.empty());

    // Data should be accessible through const reference
    const gempba::task_packet &v_const_data = v_const_ref.get_task_packet();
    EXPECT_EQ(v_const_data.size(), 1);
}

TEST(task_bundle_test, copy_constructor_creates_deep_copy) {
    const std::vector v_data = {std::byte{0x11}, std::byte{0x22}};
    const gempba::task_packet v_packet(v_data);
    const gempba::task_bundle v_original(v_packet, 100);

    const gempba::task_bundle v_copy(v_original);

    EXPECT_EQ(v_copy.get_runnable_id(), v_original.get_runnable_id());
    EXPECT_EQ(v_copy.size(), v_original.size());
    EXPECT_NE(v_copy.get_task_packet().data(), v_original.get_task_packet().data()); // Different memory

    // Verify data content is the same
    for (std::size_t i = 0; i < v_data.size(); ++i) {
        EXPECT_EQ(v_copy.get_task_packet().data()[i], v_original.get_task_packet().data()[i]);
    }
}

TEST(task_bundle_test, copy_assignment_creates_deep_copy) {
    const std::vector v_data = {std::byte{0x33}, std::byte{0x44}};
    const gempba::task_packet v_packet(v_data);
    const gempba::task_bundle v_original(v_packet, 200);

    // Create different bundle for assignment
    gempba::task_bundle v_assigned(gempba::task_packet(1), 999);
    v_assigned = v_original;

    EXPECT_EQ(v_assigned.get_runnable_id(), v_original.get_runnable_id());
    EXPECT_EQ(v_assigned.size(), v_original.size());
    EXPECT_NE(v_assigned.get_task_packet().data(), v_original.get_task_packet().data()); // Different memory
}

TEST(task_bundle_test, move_constructor_transfers_ownership) {
    gempba::task_packet v_packet(16);
    std::memset(v_packet.data(), 0xCD, v_packet.size());
    gempba::task_bundle v_original(std::move(v_packet), 300);

    const gempba::task_bundle v_moved(std::move(v_original));

    EXPECT_EQ(v_moved.get_runnable_id(), 300);
    EXPECT_EQ(v_moved.size(), 16);
    for (std::size_t i = 0; i < v_moved.size(); ++i) {
        EXPECT_EQ(v_moved.get_task_packet().data()[i], std::byte{0xCD});
    }
}

TEST(task_bundle_test, equality_operator_compares_both_fields) {
    const std::vector v_data = {std::byte{0x55}, std::byte{0x66}};
    gempba::task_packet v_packet1(v_data);
    gempba::task_packet v_packet2(v_data);

    gempba::task_bundle v_bundle1(v_packet1, 400);
    gempba::task_bundle v_bundle2(v_packet2, 400);
    gempba::task_bundle v_bundle3(v_packet1, 401); // Different function_id
    gempba::task_bundle v_bundle4(gempba::task_packet(std::vector<std::byte>{std::byte{0x77}}), 400); // Different data

    // Same data and function_id
    EXPECT_TRUE(v_bundle1 == v_bundle2);
    EXPECT_FALSE(v_bundle1 != v_bundle2);

    // Different function_id
    EXPECT_FALSE(v_bundle1 == v_bundle3);
    EXPECT_TRUE(v_bundle1 != v_bundle3);

    // Different data
    EXPECT_FALSE(v_bundle1 == v_bundle4);
    EXPECT_TRUE(v_bundle1 != v_bundle4);

    // Reflexivity
    EXPECT_TRUE(v_bundle1 == v_bundle1);
    EXPECT_FALSE(v_bundle1 != v_bundle1);
}

TEST(task_bundle_test, equality_with_empty_bundles) {
    const gempba::task_bundle v_empty1(gempba::task_packet::EMPTY, 0);
    const gempba::task_bundle v_empty2(gempba::task_packet(0), 0);
    const gempba::task_bundle v_empty_diff_id(gempba::task_packet::EMPTY, 1);

    EXPECT_TRUE(v_empty1 == v_empty2);
    EXPECT_FALSE(v_empty1 != v_empty2);
    EXPECT_FALSE(v_empty1 == v_empty_diff_id);
    EXPECT_TRUE(v_empty1 != v_empty_diff_id);
}

TEST(task_bundle_test, zero_function_id_is_valid) {
    const gempba::task_packet v_packet(std::vector{std::byte{0x99}});
    const gempba::task_bundle v_bundle(v_packet, 0);

    EXPECT_EQ(v_bundle.get_runnable_id(), 0);
    EXPECT_EQ(v_bundle.size(), 1);
}

TEST(task_bundle_test, max_uint_function_id_is_valid) {
    const gempba::task_packet v_packet(std::vector{std::byte{0x88}});
    constexpr int v_max_uint = std::numeric_limits<int>::max();
    const gempba::task_bundle v_bundle(v_packet, v_max_uint);

    EXPECT_EQ(v_bundle.get_runnable_id(), v_max_uint);
    EXPECT_EQ(v_bundle.size(), 1);
}
