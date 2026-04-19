/*
 * MIT License
 *
 * Copyright (c) 2024. Andrés Pastrana
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
#include <any>
#include <future>
#include <gempba/utils/tree.hpp>
#include <gempba/utils/utils.hpp>
#include <gtest/gtest.h>
#include <string>

/**
 * @author Andres Pastrana
 * @date 2024-08-31
 */

TEST(utils_test, convert_to_any_future_int) {

    std::promise<int> v_promise;
    std::future<int> v_future = v_promise.get_future();
    v_promise.set_value(42);

    std::future<std::any> v_any_future = utils::convert_to_any_future(std::move(v_future));
    const std::any v_result = v_any_future.get();

    ASSERT_TRUE(v_result.has_value());
    EXPECT_EQ(std::any_cast<int>(v_result), 42);
}

TEST(utils_test, convert_to_any_future_string) {
    std::promise<std::string> v_promise;
    std::future<std::string> v_future = v_promise.get_future();
    v_promise.set_value("hello");

    std::future<std::any> v_any_future = utils::convert_to_any_future(std::move(v_future));
    const std::any v_result = v_any_future.get();

    ASSERT_TRUE(v_result.has_value());
    EXPECT_EQ(std::any_cast<std::string>(v_result), "hello");
}

TEST(utils_test, build_topology_two_children_per_node) {
    int v_total = 8;
    auto v_tree_instance = tree(v_total);

    utils::build_topology(v_tree_instance, 1, 0, 2, v_total);

    EXPECT_EQ(8, v_tree_instance.size());

    EXPECT_EQ(-1, v_tree_instance[0].get_parent());
    EXPECT_EQ(-1, v_tree_instance[1].get_parent());
    EXPECT_EQ(1, v_tree_instance[2].get_parent());
    EXPECT_EQ(1, v_tree_instance[3].get_parent());
    EXPECT_EQ(2, v_tree_instance[4].get_parent());
    EXPECT_EQ(1, v_tree_instance[5].get_parent());
    EXPECT_EQ(2, v_tree_instance[6].get_parent());
    EXPECT_EQ(3, v_tree_instance[7].get_parent());

    EXPECT_EQ(0, v_tree_instance[0].size());
    EXPECT_EQ(3, v_tree_instance[1].size());
    EXPECT_EQ(2, v_tree_instance[2].size());
    EXPECT_EQ(1, v_tree_instance[3].size());
    EXPECT_EQ(0, v_tree_instance[4].size());
    EXPECT_EQ(0, v_tree_instance[5].size());
    EXPECT_EQ(0, v_tree_instance[6].size());
    EXPECT_EQ(0, v_tree_instance[7].size());
}

TEST(utils_test, build_topology_three_children_per_node) {
    int v_total = 27; // total number of processes
    auto v_tree_instance = tree(v_total);

    utils::build_topology(v_tree_instance, 1, 0, 3, v_total);

    EXPECT_EQ(27, v_tree_instance.size());

    EXPECT_EQ(-1, v_tree_instance[0].get_parent());
    EXPECT_EQ(-1, v_tree_instance[1].get_parent());
    EXPECT_EQ(1, v_tree_instance[2].get_parent());
    EXPECT_EQ(1, v_tree_instance[3].get_parent());
    EXPECT_EQ(1, v_tree_instance[4].get_parent());
    EXPECT_EQ(2, v_tree_instance[5].get_parent());
    EXPECT_EQ(3, v_tree_instance[6].get_parent());
    EXPECT_EQ(1, v_tree_instance[7].get_parent());
    EXPECT_EQ(2, v_tree_instance[8].get_parent());
    EXPECT_EQ(1, v_tree_instance[19].get_parent());
    EXPECT_EQ(2, v_tree_instance[20].get_parent());
    EXPECT_EQ(3, v_tree_instance[21].get_parent());
    EXPECT_EQ(4, v_tree_instance[22].get_parent());
    EXPECT_EQ(5, v_tree_instance[23].get_parent());
    EXPECT_EQ(6, v_tree_instance[24].get_parent());
    EXPECT_EQ(7, v_tree_instance[25].get_parent());
    EXPECT_EQ(8, v_tree_instance[26].get_parent());

    EXPECT_EQ(0, v_tree_instance[0].size());
    EXPECT_EQ(6, v_tree_instance[1].size());
    EXPECT_EQ(4, v_tree_instance[2].size());
    EXPECT_EQ(4, v_tree_instance[3].size());
    EXPECT_EQ(2, v_tree_instance[4].size());
    EXPECT_EQ(2, v_tree_instance[5].size());
    EXPECT_EQ(2, v_tree_instance[6].size());
    EXPECT_EQ(2, v_tree_instance[7].size());
    EXPECT_EQ(0, v_tree_instance[19].size());
    EXPECT_EQ(0, v_tree_instance[20].size());
    EXPECT_EQ(0, v_tree_instance[21].size());
    EXPECT_EQ(0, v_tree_instance[22].size());
    EXPECT_EQ(0, v_tree_instance[23].size());
    EXPECT_EQ(0, v_tree_instance[24].size());
    EXPECT_EQ(0, v_tree_instance[25].size());
    EXPECT_EQ(0, v_tree_instance[26].size());
}

TEST(utils_test, shift_left) {
    // Test: Fully populated vector with no -1 values

    std::vector v_vec = {1, 2, 3, 4, 5, 6};
    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({2, 3, 4, 5, 6, -1}));

    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({3, 4, 5, 6, -1, -1}));

    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({4, 5, 6, -1, -1, -1}));

    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({5, 6, -1, -1, -1, -1}));

    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({6, -1, -1, -1, -1, -1}));

    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({-1, -1, -1, -1, -1, -1}));

    // nothing else happens
    utils::shift_left(v_vec);
    EXPECT_EQ(v_vec, std::vector({-1, -1, -1, -1, -1, -1}));

    // empty vector
    try {
        std::vector<int> v_vec2;
        utils::shift_left(v_vec2);
    } catch (const std::exception &e) {
        EXPECT_STREQ(e.what(), "Attempted to shift an empty vector");
    }
}
