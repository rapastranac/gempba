#include <gtest/gtest.h>
#include <future>
#include <any>
#include <string>
#include "utils/utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-31
 */

TEST(UtilsTest, ConvertToAnyFuture_Int) {

    std::promise<int> promise;
    std::future<int> future = promise.get_future();
    promise.set_value(42);

    std::future<std::any> anyFuture = utils::convert_to_any_future(std::move(future));
    std::any result = anyFuture.get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<int>(result), 42);
}

TEST(UtilsTest, ConvertToAnyFuture_String) {
    std::promise<std::string> promise;
    std::future<std::string> future = promise.get_future();
    promise.set_value("hello");

    std::future<std::any> anyFuture = utils::convert_to_any_future(std::move(future));
    std::any result = anyFuture.get();

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::any_cast<std::string>(result), "hello");
}


TEST(UtilsTest, BuildTopologyTwoChildrenPerNode) {
    int total = 8;
    Tree tree = Tree(total);

    utils::build_topology(tree, 1, 0, 2, total);

    EXPECT_EQ(8, tree.size());

    EXPECT_EQ(-1, tree[0].getParent());
    EXPECT_EQ(-1, tree[1].getParent());
    EXPECT_EQ(1, tree[2].getParent());
    EXPECT_EQ(1, tree[3].getParent());
    EXPECT_EQ(2, tree[4].getParent());
    EXPECT_EQ(1, tree[5].getParent());
    EXPECT_EQ(2, tree[6].getParent());
    EXPECT_EQ(3, tree[7].getParent());

    EXPECT_EQ(0, tree[0].getChildrenCount());
    EXPECT_EQ(3, tree[1].getChildrenCount());
    EXPECT_EQ(2, tree[2].getChildrenCount());
    EXPECT_EQ(1, tree[3].getChildrenCount());
    EXPECT_EQ(0, tree[4].getChildrenCount());
    EXPECT_EQ(0, tree[5].getChildrenCount());
    EXPECT_EQ(0, tree[6].getChildrenCount());
    EXPECT_EQ(0, tree[7].getChildrenCount());
}

TEST(UtilsTest, BuildTopologyThreeChildrenPerNode) {
    int total = 27; // total number of processes
    Tree tree = Tree(total);

    utils::build_topology(tree, 1, 0, 3, total);

    EXPECT_EQ(27, tree.size());

    EXPECT_EQ(-1, tree[0].getParent());
    EXPECT_EQ(-1, tree[1].getParent());
    EXPECT_EQ(1, tree[2].getParent());
    EXPECT_EQ(1, tree[3].getParent());
    EXPECT_EQ(1, tree[4].getParent());
    EXPECT_EQ(2, tree[5].getParent());
    EXPECT_EQ(3, tree[6].getParent());
    EXPECT_EQ(1, tree[7].getParent());
    EXPECT_EQ(2, tree[8].getParent());
    EXPECT_EQ(1, tree[19].getParent());
    EXPECT_EQ(2, tree[20].getParent());
    EXPECT_EQ(3, tree[21].getParent());
    EXPECT_EQ(4, tree[22].getParent());
    EXPECT_EQ(5, tree[23].getParent());
    EXPECT_EQ(6, tree[24].getParent());
    EXPECT_EQ(7, tree[25].getParent());
    EXPECT_EQ(8, tree[26].getParent());

    EXPECT_EQ(0, tree[0].getChildrenCount());
    EXPECT_EQ(6, tree[1].getChildrenCount());
    EXPECT_EQ(4, tree[2].getChildrenCount());
    EXPECT_EQ(4, tree[3].getChildrenCount());
    EXPECT_EQ(2, tree[4].getChildrenCount());
    EXPECT_EQ(2, tree[5].getChildrenCount());
    EXPECT_EQ(2, tree[6].getChildrenCount());
    EXPECT_EQ(2, tree[7].getChildrenCount());
    EXPECT_EQ(0, tree[19].getChildrenCount());
    EXPECT_EQ(0, tree[20].getChildrenCount());
    EXPECT_EQ(0, tree[21].getChildrenCount());
    EXPECT_EQ(0, tree[22].getChildrenCount());
    EXPECT_EQ(0, tree[23].getChildrenCount());
    EXPECT_EQ(0, tree[24].getChildrenCount());
    EXPECT_EQ(0, tree[25].getChildrenCount());
    EXPECT_EQ(0, tree[26].getChildrenCount());
}


TEST(UtilsTest, ShiftLeft) {
    // Test: Fully populated vector with no -1 values
    {
        std::vector<int> vec = {1, 2, 3, 4, 5, 6};
        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({2, 3, 4, 5, 6, -1}));

        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({3, 4, 5, 6, -1, -1}));

        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({4, 5, 6, -1, -1, -1}));

        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({5, 6, -1, -1, -1, -1}));

        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({6, -1, -1, -1, -1, -1}));

        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({-1, -1, -1, -1, -1, -1}));

        //nothing else happens
        utils::shift_left(vec);
        EXPECT_EQ(vec, std::vector<int>({-1, -1, -1, -1, -1, -1}));

        // empty vector
        try {
            std::vector<int> vec2 = {7};
            utils::shift_left(vec2);
            EXPECT_EQ(vec2, std::vector<int>({7}));
        } catch (const std::exception &e) {
            EXPECT_STRNE(e.what(), "Attempted to shift an empty vector");
        }
    }
}