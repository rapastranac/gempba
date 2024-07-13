#include <gtest/gtest.h>
#include "../../../GemPBA/utils/Tree.hpp"

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