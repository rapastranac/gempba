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

#include <boost/archive/binary_oarchive.hpp>
#include <functional>
#include <sstream>

#include "utils/ExecutorImpl.hpp"

TEST(ExecutorTest, ExecutorImplNonVoid) {

    std::function<double(int, int, float, double, void *)> foo = [](int id, int ivalue, float fvalue, double dvalue, void *dummy) {
        return static_cast<double>(ivalue) + fvalue + dvalue;
    };

    gempba::NodeManager &node_manager = gempba::NodeManager::getInstance();
    node_manager.initThreadPool(1);
    gempba::ExecutorImpl<double(int, int, float, double, void *)> executorImpl(foo, node_manager);

    // Create an Executor* that should point to executorImpl if it is indeed an implementation
    auto *executor = dynamic_cast<gempba::Executor<double(int, int, float, double, void *)> *>(&executorImpl);
    EXPECT_TRUE(executor != nullptr) << "executorImpl is not an instance of Executor";

    int val1 = 1;
    float val2 = 2.4f;
    double val3 = 3.54;

    // Create an archive to serialize the values
    std::ostringstream oss;
    boost::archive::binary_oarchive oa(oss);

    // Serialize each value into the archive
    oa << val1;
    oa << val2;
    oa << val3;

    // Get the serialized string from the output stream
    std::string serialized_arguments = oss.str();

    std::shared_future<std::string> f = executorImpl(serialized_arguments);

    auto start_time = std::chrono::high_resolution_clock::now();
    double timeout = 3.0;
    while (!gempba::is_future_ready(f)) {
        auto current_time = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count() > timeout) {
            FAIL();
        }
    }

    const std::string &serialized_result = f.get();

    std::istringstream iss(serialized_result);
    boost::archive::binary_iarchive ia(iss);

    double deserialized_result;

    ia >> deserialized_result;

    ASSERT_EQ(6.9400000953674317, deserialized_result);
}

TEST(ExecutorTest, ExecutorImplVoid) {

    std::atomic<double> test_buffer;

    std::function<void(int, int, float, double, void *)> foo = [&test_buffer](int id, int ivalue, float fvalue, double dvalue, void *dummy) {
        auto value = static_cast<double>(ivalue) + fvalue + dvalue;
        test_buffer.store(value, std::memory_order_relaxed);
    };

    gempba::NodeManager &node_manager = gempba::NodeManager::getInstance();
    node_manager.initThreadPool(1);
    gempba::ExecutorImpl<void(int, int, float, double, void *)> executorImpl(foo, node_manager);

    // Create an Executor* that should point to executorImpl if it is indeed an implementation
    auto *executor = dynamic_cast<gempba::Executor<void(int, int, float, double, void *)> *>(&executorImpl);
    EXPECT_TRUE(executor != nullptr) << "executorImpl is not an instance of Executor";

    int val1 = 1;
    float val2 = 2.4f;
    double val3 = 3.54;

    // Create an archive to serialize the values
    std::ostringstream oss;
    boost::archive::binary_oarchive oa(oss);

    // Serialize each value into the archive
    oa << val1;
    oa << val2;
    oa << val3;

    // Get the serialized string from the output stream
    std::string serialized_arguments = oss.str();

    std::shared_future<void> f = executorImpl(serialized_arguments);

    auto start_time = std::chrono::high_resolution_clock::now();
    double timeout = 3.0;
    std::chrono::nanoseconds elapsed_time;
    while (!gempba::is_future_ready(f)) {
        auto current_time = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time).count() > timeout) {
            FAIL();
        }
        // update elapsed time
        elapsed_time = std::chrono::duration_cast<std::chrono::nanoseconds>(current_time - start_time);
    }


    ASSERT_EQ(6.9400000953674317, test_buffer.load());
}