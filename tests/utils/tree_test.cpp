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
#include "utils/Tree.hpp"

TEST(TreeTest, Constructor) {
    Tree tree;
    EXPECT_EQ(tree.size(), 0);
}

TEST(TreeTest, ConstructorWithSize) {
    Tree tree(10);
    EXPECT_EQ(tree.size(), 10);
}

TEST(TreeTest, Resize) {
    Tree tree;
    tree.resize(5);
    EXPECT_EQ(tree.size(), 5);
}

TEST(TreeTest, NodeAssignment) {
    Tree tree(2);
    int idx1 = 0;
    int idx2 = 1;
    tree[idx1].addNext(idx2);

    EXPECT_EQ(tree[idx2].getParent(), idx1);
}

TEST(TreeTest, NodeUnlinking) {
    Tree tree(3);
    int idx1 = 0;
    int idx2 = 1;
    int idx3 = 2;
    tree[idx1].addNext(idx2);
    tree[idx2].addNext(idx3);
    tree[idx1].pop_front();
    EXPECT_EQ(tree[idx2].getParent(), -1);
}

TEST(TreeTest, PopFrontNode) {
    Tree t(3);
    t[0].addNext(1);
    t[0].addNext(2);
    EXPECT_EQ(t[0].getNext(), 1);
    t[0].pop_front();
    EXPECT_EQ(t[0].getNext(), 2);
    EXPECT_EQ(t[1].getParent(), -1);
}

TEST(TreeTest, ClearNodes) {
    Tree t(3);
    t[0].addNext(1);
    t[0].addNext(2);
    t[0].clear();
    EXPECT_EQ(t[0].size(), 0);
    EXPECT_EQ(t[0].getNext(), -1);
}

TEST(TreeTest, NodeReleasing) {
    Tree tree(2);
    int idx1 = 0;
    int idx2 = 1;
    tree[idx1].addNext(idx2);
    tree[idx2].release();
    EXPECT_EQ(tree[idx1].size(), 0);
}

TEST(TreeTest, IteratorTest) {
    Tree t(3);
    t[0].addNext(1);
    t[0].addNext(2);

    std::vector<int> expected = {1, 2};
    std::vector<int> result;
    for (int & it : t[0]) {
        result.push_back(it);
    }
    EXPECT_EQ(result, expected);
}

TEST(TreeTest, InvalidAddNext) {
    Tree t(3);
    t[0].addNext(1);
    EXPECT_THROW(t[0].addNext(1), std::runtime_error);
}

TEST(TreeTest, InvalidPopFront) {
    Tree t(1);
    EXPECT_THROW(t[0].pop_front(), std::runtime_error);
}

TEST(TreeTest, InvalidRelease) {
    Tree t(1);
    EXPECT_THROW(t[0].release(), std::runtime_error);
}