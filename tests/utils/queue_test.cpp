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
#include <memory>
#include <gtest/gtest.h>
#include <utils/Queue.hpp>

struct my_struct {
    int m_value;
};

TEST(queue_test, push_and_pop) {
    Queue<my_struct *> q;

    auto *a = new my_struct{1};
    auto *b = new my_struct{2};
    auto *c = new my_struct{3};

    // Push some values into the queue
    ASSERT_TRUE(q.push(a));
    ASSERT_TRUE(q.push(b));
    ASSERT_TRUE(q.push(c));

    // Pop the values and ensure they are retrieved in the correct order
    my_struct *result;
    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(a, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(b, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(c, result);

    // Ensure the queue is empty after popping all elements
    EXPECT_TRUE(q.empty());
}

TEST(queue_test, push_and_pop_with_shared_pointer) {
    Queue<std::shared_ptr<my_struct> > q;

    const auto a = std::make_shared<my_struct>(1);
    const auto b = std::make_shared<my_struct>(2);
    const auto c = std::make_shared<my_struct>(3);

    // Push some values into the queue
    ASSERT_TRUE(q.push(a));
    ASSERT_TRUE(q.push(b));
    ASSERT_TRUE(q.push(c));

    // Pop the values and ensure they are retrieved in the correct order
    std::shared_ptr<my_struct> result;
    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(a, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(b, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(c, result);

    // Ensure the queue is empty after popping all elements
    EXPECT_TRUE(q.empty());
}


TEST(queue_test, empty) {
    Queue<my_struct *> q;

    auto *a = new my_struct{1};
    auto *b = new my_struct{2};

    // Queue should be empty initially
    EXPECT_TRUE(q.empty());

    // Push some values into the queue
    q.push(a);
    q.push(b);

    // Queue should not be empty after pushing elements
    EXPECT_FALSE(q.empty());

    // Pop all elements
    my_struct *instance;
    ASSERT_TRUE(q.pop(instance));
    ASSERT_TRUE(q.pop(instance));

    // Queue should be empty again after popping all elements
    EXPECT_TRUE(q.empty());
}
