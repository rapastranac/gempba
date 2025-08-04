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
#include <utils/ipc/result.hpp>

#include <gtest/gtest.h>

TEST(result_test, empty_singleton_has_expected_defaults) {
    const gempba::result &v_empty = gempba::result::EMPTY;

    EXPECT_EQ(v_empty.get_reference_value(), -1);
    EXPECT_TRUE(v_empty.get_task_packet().empty());
}

TEST(result_test, constructor_initializes_members_correctly) {
    const std::string v_message = "hello";
    gempba::task_packet v_packet(v_message);

    gempba::result v_res(42, v_packet);

    EXPECT_EQ(v_res.get_reference_value(), 42);
    EXPECT_EQ(v_res.get_task_packet().size(), v_packet.size());

    for (std::size_t i = 0; i < v_packet.size(); ++i) {
        EXPECT_EQ(v_res.get_task_packet().data()[i], v_packet.data()[i]);
    }
}

TEST(result_test, copy_constructor_copies_values) {
    const gempba::task_packet v_packet("abc");
    const gempba::result v_original(7, v_packet);
    const gempba::result &v_copy(v_original);

    EXPECT_EQ(v_copy.get_reference_value(), 7);
    EXPECT_EQ(v_copy.get_task_packet().size(), 3);
}

TEST(result_test, move_constructor_moves_values) {
    const gempba::task_packet v_packet("xyz");
    gempba::result v_original(123, v_packet);
    const gempba::result v_moved(std::move(v_original));

    EXPECT_EQ(v_moved.get_reference_value(), 123);
    EXPECT_EQ(v_moved.get_task_packet().size(), 3);
}

TEST(result_test, copy_assignment_copies_values) {
    const gempba::task_packet v_packet("123");
    const gempba::result v_a(1, v_packet);
    gempba::result v_b(2, gempba::task_packet("456"));

    v_b = v_a;

    EXPECT_EQ(v_b.get_reference_value(), 1);
    EXPECT_EQ(v_b.get_task_packet().size(), 3);
    EXPECT_EQ(v_b.get_task_packet().data()[0], std::byte{'1'});
}

TEST(result_test, move_assignment_moves_values) {
    gempba::result v_a(11, gempba::task_packet("data"));
    gempba::result v_b(22, gempba::task_packet("temp"));

    v_b = std::move(v_a);

    EXPECT_EQ(v_b.get_reference_value(), 11);
    EXPECT_EQ(v_b.get_task_packet().size(), 4);
}


TEST(result_test, equality_operator_behaves_correctly) {
    const gempba::result v_result1(10, gempba::task_packet(5));
    const gempba::result v_result2(10, gempba::task_packet(5));
    const gempba::result v_result3(20, gempba::task_packet(5));
    const gempba::result v_result4(10, gempba::task_packet(6));
    const gempba::result v_default_empty = gempba::result::EMPTY;
    const gempba::result v_manual_empty(-1, gempba::task_packet::EMPTY);

    // Equal when all fields match
    EXPECT_TRUE(v_result1 == v_result2);

    // Not equal when reference values differ
    EXPECT_FALSE(v_result1 == v_result3);

    // Not equal when task packets differ
    EXPECT_FALSE(v_result1 == v_result4);

    // Reflexivity: equal to itself
    EXPECT_TRUE(v_result1 == v_result1);

    // Equal to predefined EMPTY
    EXPECT_TRUE(v_default_empty == v_manual_empty);
}


TEST(result_test, empty_instance_is_consistent) {
    const gempba::result v_empty1 = gempba::result::EMPTY;
    const gempba::result v_empty2(-1, gempba::task_packet::EMPTY);

    // EMPTY should be equal to manually created empty result
    EXPECT_TRUE(v_empty1 == v_empty2);

    // EMPTY should be equal to itself
    EXPECT_TRUE(v_empty1 == v_empty1);
}
