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

#include <gtest/gtest.h>
#include <utils/tree.hpp>

TEST(tree_test, constructor) {
    const tree instance;
    EXPECT_EQ(instance.size(), 0);
}

TEST(tree_test, constructor_with_size) {
    const tree instance(10);
    EXPECT_EQ(instance.size(), 10);
}

TEST(tree_test, resize) {
    tree instance;
    instance.resize(5);
    EXPECT_EQ(instance.size(), 5);
}

TEST(tree_test, node_assignment) {
    tree instance(2);
    constexpr int idx1 = 0;
    constexpr int idx2 = 1;
    instance[idx1].add_next(idx2);

    EXPECT_EQ(instance[idx2].get_parent(), idx1);
}

TEST(tree_test, node_unlinking) {
    tree instance(3);
    constexpr int idx1 = 0;
    constexpr int idx2 = 1;
    constexpr int idx3 = 2;
    instance[idx1].add_next(idx2);
    instance[idx2].add_next(idx3);
    instance[idx1].pop_front();
    EXPECT_EQ(instance[idx2].get_parent(), -1);
}

TEST(tree_test, pop_front_node) {
    tree instance(3);
    instance[0].add_next(1);
    instance[0].add_next(2);
    EXPECT_EQ(instance[0].get_next(), 1);
    instance[0].pop_front();
    EXPECT_EQ(instance[0].get_next(), 2);
    EXPECT_EQ(instance[1].get_parent(), -1);
}

TEST(tree_test, clear_nodes) {
    tree instance(3);
    instance[0].add_next(1);
    instance[0].add_next(2);
    instance[0].clear();
    EXPECT_EQ(instance[0].size(), 0);
    EXPECT_EQ(instance[0].get_next(), -1);
}

TEST(tree_test, node_releasing) {
    tree instance(2);
    constexpr int idx1 = 0;
    constexpr int idx2 = 1;
    instance[idx1].add_next(idx2);
    instance[idx2].release();
    EXPECT_EQ(instance[idx1].size(), 0);
}

TEST(tree_test, iterator_test) {
    tree instance(3);
    instance[0].add_next(1);
    instance[0].add_next(2);

    const std::vector expected = {1, 2};
    std::vector<int> result;
    for (int &it: instance[0]) {
        result.push_back(it);
    }
    EXPECT_EQ(result, expected);
}

TEST(tree_test, invalid_add_next) {
    tree instance(3);
    instance[0].add_next(1);
    EXPECT_THROW(instance[0].add_next(1), spdlog::spdlog_ex);
}

TEST(tree_test, invalid_pop_front) {
    tree instance(1);
    EXPECT_THROW(instance[0].pop_front(), spdlog::spdlog_ex);
}

TEST(tree_test, invalid_release) {
    tree instance(1);
    EXPECT_THROW(instance[0].release(), spdlog::spdlog_ex);
}
