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

    EXPECT_EQ(v_empty.get_score_as_integer(), -1);
    EXPECT_TRUE(v_empty.get_task_packet().empty());
}

TEST(result_test, constructor_initializes_members_correctly) {
    const std::string v_message = "hello";
    gempba::task_packet v_packet(v_message);

    const gempba::score v_score = gempba::score::make(42);
    const gempba::result v_res(v_score, v_packet);

    EXPECT_EQ(v_res.get_score_as_integer(), 42);
    EXPECT_EQ(v_res.get_task_packet().size(), v_packet.size());

    for (std::size_t i = 0; i < v_packet.size(); ++i) {
        EXPECT_EQ(v_res.get_task_packet().data()[i], v_packet.data()[i]);
    }
}

TEST(result_test, copy_constructor_copies_values) {
    const gempba::task_packet v_packet("abc");
    const gempba::score v_score = gempba::score::make(7);
    const gempba::result v_original(v_score, v_packet);
    const gempba::result &v_copy(v_original);

    EXPECT_EQ(v_copy.get_score_as_integer(), 7);
    EXPECT_EQ(v_copy.get_task_packet().size(), 3);
}

TEST(result_test, move_constructor_moves_values) {
    const gempba::task_packet v_packet("xyz");
    const gempba::score v_score = gempba::score::make(123);
    gempba::result v_original(v_score, v_packet);
    const gempba::result v_moved(std::move(v_original));

    EXPECT_EQ(v_moved.get_score_as_integer(), 123);
    EXPECT_EQ(v_moved.get_task_packet().size(), 3);
}

TEST(result_test, copy_assignment_copies_values) {
    const gempba::task_packet v_packet("123");
    const gempba::score v_score1 = gempba::score::make(1);
    const gempba::result v_a(v_score1, v_packet);
    const gempba::score v_score2 = gempba::score::make(2);
    gempba::result v_b(v_score2, gempba::task_packet("456"));

    v_b = v_a;

    EXPECT_EQ(v_b.get_score_as_integer(), 1);
    EXPECT_EQ(v_b.get_task_packet().size(), 3);
    EXPECT_EQ(v_b.get_task_packet().data()[0], std::byte{'1'});
}

TEST(result_test, move_assignment_moves_values) {
    const gempba::score v_score1 = gempba::score::make(11);
    gempba::result v_a(v_score1, gempba::task_packet("data"));
    const gempba::score v_score2 = gempba::score::make(22);
    gempba::result v_b(v_score2, gempba::task_packet("temp"));

    v_b = std::move(v_a);

    EXPECT_EQ(v_b.get_score_as_integer(), 11);
    EXPECT_EQ(v_b.get_task_packet().size(), 4);
}


TEST(result_test, equality_operator_behaves_correctly) {
    const gempba::score v_score1 = gempba::score::make(10);
    const gempba::score v_score2 = gempba::score::make(20);
    const gempba::score v_score3 = gempba::score::make(-1);

    const gempba::result v_result1(v_score1, gempba::task_packet(5));
    const gempba::result v_result2(v_score1, gempba::task_packet(5));
    const gempba::result v_result3(v_score2, gempba::task_packet(5));
    const gempba::result v_result4(v_score1, gempba::task_packet(6));
    const gempba::result v_default_empty = gempba::result::EMPTY;
    const gempba::result v_manual_empty(v_score3, gempba::task_packet::EMPTY);

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
    const gempba::score v_score = gempba::score::make(-1);
    const gempba::result v_empty2(v_score, gempba::task_packet::EMPTY);

    // EMPTY should be equal to manually created empty result
    EXPECT_TRUE(v_empty1 == v_empty2);

    // EMPTY should be equal to itself
    EXPECT_TRUE(v_empty1 == v_empty1);
}
