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

#include <filesystem>
#include <fstream>
#include <gempba/utils/tree.hpp>
#include <gtest/gtest.h>

TEST(tree_test, constructor) {
    const tree v_instance;
    EXPECT_EQ(v_instance.size(), 0);
}

TEST(tree_test, constructor_with_size) {
    const tree v_instance(10);
    EXPECT_EQ(v_instance.size(), 10);
}

TEST(tree_test, resize) {
    tree v_instance;
    v_instance.resize(5);
    EXPECT_EQ(v_instance.size(), 5);
}

TEST(tree_test, node_assignment) {
    tree v_instance(2);
    constexpr int v_idx1 = 0;
    constexpr int v_idx2 = 1;
    v_instance[v_idx1].add_next(v_idx2);

    EXPECT_EQ(v_instance[v_idx2].get_parent(), v_idx1);
}

TEST(tree_test, node_unlinking) {
    tree v_instance(3);
    constexpr int v_idx1 = 0;
    constexpr int v_idx2 = 1;
    constexpr int v_idx3 = 2;
    v_instance[v_idx1].add_next(v_idx2);
    v_instance[v_idx2].add_next(v_idx3);
    v_instance[v_idx1].pop_front();
    EXPECT_EQ(v_instance[v_idx2].get_parent(), -1);
}

TEST(tree_test, pop_front_node) {
    tree v_instance(3);
    v_instance[0].add_next(1);
    v_instance[0].add_next(2);
    EXPECT_EQ(v_instance[0].get_next(), 1);
    v_instance[0].pop_front();
    EXPECT_EQ(v_instance[0].get_next(), 2);
    EXPECT_EQ(v_instance[1].get_parent(), -1);
}

TEST(tree_test, clear_nodes) {
    tree v_instance(3);
    v_instance[0].add_next(1);
    v_instance[0].add_next(2);
    v_instance[0].clear();
    EXPECT_EQ(v_instance[0].size(), 0);
    EXPECT_EQ(v_instance[0].get_next(), -1);
}

TEST(tree_test, node_releasing) {
    // Test 1: Release the only child
    {
        tree v_instance(2);
        v_instance[0].add_next(1);
        EXPECT_EQ(v_instance[0].size(), 1);
        EXPECT_EQ(v_instance[1].get_parent(), 0);

        v_instance[1].release();

        EXPECT_EQ(v_instance[0].size(), 0);
        EXPECT_EQ(v_instance[0].get_next(), -1);
        EXPECT_EQ(v_instance[1].get_parent(), -1);
        EXPECT_FALSE(v_instance[1].is_assigned());
    }

    // Test 2: Release the first child (when there are multiple children)
    {
        tree v_instance(4);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);
        v_instance[0].add_next(3);
        EXPECT_EQ(v_instance[0].size(), 3);
        EXPECT_EQ(v_instance[0].get_next(), 1);

        v_instance[1].release();

        EXPECT_EQ(v_instance[0].size(), 2);
        EXPECT_EQ(v_instance[0].get_next(), 2); // Node 2 should now be first
        EXPECT_EQ(v_instance[1].get_parent(), -1);
        EXPECT_FALSE(v_instance[1].is_assigned());
        EXPECT_EQ(v_instance[2].get_parent(), 0);
        EXPECT_EQ(v_instance[3].get_parent(), 0);
    }

    // Test 3: Release the last child
    {
        tree v_instance(4);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);
        v_instance[0].add_next(3);
        EXPECT_EQ(v_instance[0].size(), 3);

        v_instance[3].release();

        EXPECT_EQ(v_instance[0].size(), 2);
        EXPECT_EQ(v_instance[3].get_parent(), -1);
        EXPECT_FALSE(v_instance[3].is_assigned());
        EXPECT_EQ(v_instance[1].get_parent(), 0);
        EXPECT_EQ(v_instance[2].get_parent(), 0);

        // Verify that node 2 is now the last child
        std::vector<int> v_children;
        for (int &v_child: v_instance[0]) {
            v_children.push_back(v_child);
        }
        EXPECT_EQ(v_children.back(), 2);
    }

    // Test 4: Release a middle child
    {
        tree v_instance(5);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);
        v_instance[0].add_next(3);
        v_instance[0].add_next(4);
        EXPECT_EQ(v_instance[0].size(), 4);

        v_instance[2].release(); // Release middle child

        EXPECT_EQ(v_instance[0].size(), 3);
        EXPECT_EQ(v_instance[2].get_parent(), -1);
        EXPECT_FALSE(v_instance[2].is_assigned());

        // Verify the remaining children are still properly linked
        std::vector<int> v_expected = {1, 3, 4};
        std::vector<int> v_result;
        for (int &v_child: v_instance[0]) {
            v_result.push_back(v_child);
        }
        EXPECT_EQ(v_result, v_expected);
    }

    // Test 5: Release another middle child to ensure sibling links are correct
    {
        tree v_instance(6);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);
        v_instance[0].add_next(3);
        v_instance[0].add_next(4);
        v_instance[0].add_next(5);

        v_instance[3].release(); // Release middle child

        EXPECT_EQ(v_instance[0].size(), 4);

        std::vector<int> v_expected = {1, 2, 4, 5};
        std::vector<int> v_result;
        for (int &v_child: v_instance[0]) {
            v_result.push_back(v_child);
        }
        EXPECT_EQ(v_result, v_expected);
    }

    // Test 6: Sequential releases
    {
        tree v_instance(4);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);
        v_instance[0].add_next(3);

        v_instance[2].release(); // Release middle
        EXPECT_EQ(v_instance[0].size(), 2);

        v_instance[3].release(); // Release what's now the last
        EXPECT_EQ(v_instance[0].size(), 1);

        v_instance[1].release(); // Release the only remaining child
        EXPECT_EQ(v_instance[0].size(), 0);
        EXPECT_EQ(v_instance[0].get_next(), -1);
    }

    // Test 7: Release and re-add
    {
        tree v_instance(3);
        v_instance[0].add_next(1);
        v_instance[0].add_next(2);

        v_instance[1].release();
        EXPECT_EQ(v_instance[0].size(), 1);
        EXPECT_FALSE(v_instance[1].is_assigned());

        // Should be able to add it again
        v_instance[0].add_next(1);
        EXPECT_EQ(v_instance[0].size(), 2);
        EXPECT_TRUE(v_instance[1].is_assigned());
        EXPECT_EQ(v_instance[1].get_parent(), 0);
    }
}

TEST(tree_test, iterator_test) {
    tree v_instance(3);
    v_instance[0].add_next(1);
    v_instance[0].add_next(2);

    const std::vector v_expected = {1, 2};
    std::vector<int> v_result;
    for (int &it: v_instance[0]) {
        v_result.push_back(it);
    }
    EXPECT_EQ(v_result, v_expected);
}

TEST(tree_test, invalid_add_next) {
    tree v_instance(3);
    v_instance[0].add_next(1);
    EXPECT_THROW(v_instance[0].add_next(1), std::runtime_error);
}

TEST(tree_test, invalid_pop_front) {
    tree v_instance(1);
    EXPECT_THROW(v_instance[0].pop_front(), std::runtime_error);
}

TEST(tree_test, invalid_release) {
    tree v_instance(1);
    EXPECT_THROW(v_instance[0].release(), std::runtime_error);
}

std::string get_test_resource_path(const std::string &p_name) {
    namespace fs = std::filesystem;
    const fs::path v_file_path = __FILE__; // expands to something like /path/to/tests/test_tree.cpp
    return (v_file_path.parent_path().parent_path().parent_path() / "resources" / p_name).string();
}

std::string load_resource(const std::string &p_filename) {
    std::ifstream v_file(get_test_resource_path(p_filename));
    if (!v_file.is_open()) {
        throw std::runtime_error("Could not open resource: " + p_filename);
    }

    std::ostringstream v_ss;
    v_ss << v_file.rdbuf();
    return v_ss.str();
}


TEST(tree_test, to_string_test) {

    {
        tree v_tree(2);
        v_tree[0].add_next(1);

        const std::string v_output = v_tree.to_string();
        const std::string v_expected = load_resource("tree0");

        ASSERT_EQ(v_expected, v_output);
    }

    {
        tree v_tree(8);
        v_tree[0].add_next(1);
        v_tree[0].add_next(2);
        v_tree[0].add_next(3);
        v_tree[0].add_next(4);
        v_tree[0].add_next(5);
        v_tree[0].add_next(6);
        v_tree[0].add_next(7);

        const std::string v_output = v_tree.to_string();
        const std::string v_expected = load_resource("tree1");

        ASSERT_EQ(v_expected, v_output);
    }

    {
        tree v_tree(9);
        v_tree[0].add_next(1);
        v_tree[0].add_next(2);

        v_tree[1].add_next(3);
        v_tree[1].add_next(4);
        v_tree[1].add_next(5);

        v_tree[2].add_next(6);
        v_tree[2].add_next(7);
        v_tree[2].add_next(8);

        const std::string v_output = v_tree.to_string();
        const std::string v_expected = load_resource("tree2");

        ASSERT_EQ(v_expected, v_output);
    }
}
