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

#include <functional>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <gtest/gtest.h>

#include <load_balancing/impl/work_stealing_load_balancer.hpp>
#include <node_trace/api/node.hpp>
#include <runnables/factory/serial_runnable_factory.hpp>


TEST(serial_runnable_void_test, test) {
    std::function<void(std::thread::id, int, double, gempba::node)> v_function = [](std::thread::id, int p_integer, double p_double, gempba::node) {
        ASSERT_EQ(7, p_integer);
        ASSERT_DOUBLE_EQ(1.6825127784311510, p_double);
    };
    const std::function<std::tuple<int, double>(const gempba::task_packet &&p_task)> v_args_deserializer = [](const gempba::task_packet &&p_buffer) {
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_buffer.data()), static_cast<int>(p_buffer.size()));
        std::string v_token;

        std::vector<std::string> v_tokens;
        while (std::getline(v_ss, v_token, ',')) {
            v_tokens.push_back(v_token);
        }

        const int v_ival = std::stoi(v_tokens[0]);
        const double v_dval = std::stod(v_tokens[1]);

        return std::make_tuple(v_ival, v_dval);
    };
    constexpr int v_id = 314;

    const std::shared_ptr<gempba::serial_runnable> v_runnable = gempba::serial_runnable_factory::return_none::create(v_id, v_function, v_args_deserializer);

    ASSERT_EQ(314, v_runnable->get_id());

    gempba::load_balancer *v_load_balancer = new gempba::work_stealing_load_balancer(nullptr);
    gempba::branch_handler::reset_instance();
    gempba::branch_handler &v_branch_handler = gempba::branch_handler::create(v_load_balancer);
    const auto v_bytes = gempba::task_packet("7,1.6825127784311510");
    const std::optional<std::shared_future<gempba::task_packet> > v_optional = (*v_runnable)(v_branch_handler, v_bytes);

    ASSERT_FALSE(v_optional.has_value());

    v_branch_handler.wait2(); // gives time to the internal thread pool to execute the function
}
