#include <gtest/gtest.h>
#include <memory>
#include "../../../GemPBA/utils/Queue.hpp"

struct MyStruct {
    int value;
};

TEST(QueueTest, PushAndPop) {
    Queue<MyStruct *> q;

    auto *a = new MyStruct{1};
    auto *b = new MyStruct{2};
    auto *c = new MyStruct{3};

// Push some values into the queue
    ASSERT_TRUE(q.push(a));
    ASSERT_TRUE(q.push(b));
    ASSERT_TRUE(q.push(c));

// Pop the values and ensure they are retrieved in the correct order
    MyStruct *result;
    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(a, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(b, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(c, result);

// Ensure the queue is empty after popping all elements
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, PushAndPopWithSharedPointer) {
    Queue<std::shared_ptr<MyStruct>> q;

    std::shared_ptr<MyStruct> a = std::make_shared<MyStruct>(1);
    std::shared_ptr<MyStruct> b = std::make_shared<MyStruct>(2);
    std::shared_ptr<MyStruct> c = std::make_shared<MyStruct>(3);

// Push some values into the queue
    ASSERT_TRUE(q.push(a));
    ASSERT_TRUE(q.push(b));
    ASSERT_TRUE(q.push(c));

// Pop the values and ensure they are retrieved in the correct order
    std::shared_ptr<MyStruct> result;
    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(a, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(b, result);

    ASSERT_TRUE(q.pop(result));
    EXPECT_EQ(c, result);

// Ensure the queue is empty after popping all elements
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, Empty) {
    Queue<MyStruct *> q;

    auto *a = new MyStruct{1};
    auto *b = new MyStruct{2};

// Queue should be empty initially
    EXPECT_TRUE(q.empty());

// Push some values into the queue
    q.push(a);
    q.push(b);

// Queue should not be empty after pushing elements
    EXPECT_FALSE(q.empty());

// Pop all elements
    MyStruct *instance;
    ASSERT_TRUE(q.pop(instance));
    ASSERT_TRUE(q.pop(instance));

// Queue should be empty again after popping all elements
    EXPECT_TRUE(q.empty());
}

