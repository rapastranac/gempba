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
