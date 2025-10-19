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

#include <gempba/gempba.hpp>
#include <gempba/core/node.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>


TEST(serial_runnable_non_void_test, test) {
    int v_int_for_test;
    double v_double_for_test;

    std::function<double(std::thread::id, int, double, gempba::node)> v_function = [&](std::thread::id, int p_i_val, double p_f_val, gempba::node) {
        v_int_for_test = p_i_val;
        v_double_for_test = p_f_val;
        return p_i_val * p_f_val;
    };
    const std::function<std::tuple<int, double>(const gempba::task_packet &&)> v_args_deserializer = [](const gempba::task_packet &&p_bytes) {
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_bytes.data()), static_cast<int>(p_bytes.size()));
        std::string v_token;

        std::vector<std::string> v_tokens;
        while (std::getline(v_ss, v_token, ',')) {
            v_tokens.push_back(v_token);
        }

        const int v_integer = std::stoi(v_tokens[0]);
        const double v_double = std::stod(v_tokens[1]);

        return std::make_tuple(v_integer, v_double);
    };
    std::function<gempba::task_packet(double)> v_result_serializer = [](const double p_value) {
        std::ostringstream v_oss;
        v_oss << std::setprecision(16) << p_value;
        const std::string v_result = v_oss.str();
        return gempba::task_packet(v_result);
    };
    constexpr int v_id = 548;
    const std::shared_ptr<gempba::serial_runnable> v_runnable = gempba::mp::runnables::return_value::create<double>(
            v_id, v_function, v_args_deserializer, v_result_serializer);

    ASSERT_EQ(548, v_runnable->get_id());

    gempba::load_balancer *v_load_balancer = new gempba::work_stealing_load_balancer(nullptr);
    gempba::branch_handler v_branch_handler(v_load_balancer, nullptr);
    const std::optional<std::shared_future<gempba::task_packet> > v_optional = (*v_runnable)(v_branch_handler, gempba::task_packet("7,1.6825127784311510"));

    ASSERT_TRUE(v_optional.has_value());
    const std::shared_future<gempba::task_packet> &v_future = v_optional.value();

    const std::string v_expected = "11.77758944901806";
    const gempba::task_packet &v_actual = v_future.get();
    std::string v_actual_string(reinterpret_cast<const char *>(v_actual.data()), v_actual.size());
    ASSERT_STREQ(v_expected.c_str(), v_actual_string.c_str());

    const double v_actual_value = std::stod(v_expected);
    constexpr double v_expected_value = 11.777589449018061;
    ASSERT_DOUBLE_EQ(v_expected_value, v_actual_value);

    ASSERT_EQ(7, v_int_for_test);
    ASSERT_DOUBLE_EQ(1.6825127784311510, v_double_for_test);
}
