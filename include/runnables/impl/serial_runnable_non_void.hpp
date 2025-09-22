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

#ifndef SERIAL_RUNNABLE_NON_VOID_HPP
#define SERIAL_RUNNABLE_NON_VOID_HPP

#include <functional>
#include <future>
#include <optional>
#include <branch_handling/branch_handler.hpp>
#include <node_trace/api/node.hpp>
#include <runnables/api/serial_runnable.hpp>

namespace gempba {
    template<typename T>
    class serial_runnable_non_void;

    template<typename R, typename... Args>
    class serial_runnable_non_void<R(Args...)> final : public serial_runnable {
        const int m_id;
        std::function<R(std::thread::id, Args..., node)> m_f;


        std::function<std::tuple<Args...>(const task_packet &&)> m_args_deserializer;
        std::function<task_packet(R)> m_result_serializer;

    public:
        serial_runnable_non_void(const int p_id, invokable<R, Args...> auto &&p_f, std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer,
                                 std::function<task_packet(R)> p_result_serializer) :
            m_id(p_id), m_f(p_f), m_args_deserializer(p_args_deserializer), m_result_serializer(p_result_serializer) {
        }

        ~serial_runnable_non_void() override = default;

        [[nodiscard]] int get_id() const override { return m_id; }

        std::optional<std::shared_future<task_packet> > operator()(branch_handler &p_branch_handler, const task_packet &p_args) override {
            std::tuple<Args...> v_user_args = m_args_deserializer(std::move(p_args));

            std::future<std::any> v_fut = p_branch_handler.force_local_submit([this, v_user_args = std::move(v_user_args)] {
                auto v_all_args = std::tuple_cat(std::make_tuple(std::this_thread::get_id()), v_user_args, std::make_tuple<node>(node()));
                return std::apply(m_f, v_all_args);
            });

            std::future<R> v_future_actual = std::async(std::launch::async, [this, v_future = v_fut.share()] {
                //spawns another thread, but it will be just waiting until the future is ready
                std::any v_any = v_future.get();
                return std::any_cast<R>(v_any);
            });

            std::future<task_packet> v_fr = std::async(std::launch::async, [this, v_future = v_future_actual.share()] {
                R v_result = v_future.get();
                task_packet v_serialized_result = m_result_serializer(v_result);
                return v_serialized_result;
            });
            return std::make_optional(v_fr.share());
        }
    };
} // namespace gempba

#endif // SERIAL_RUNNABLE_NON_VOID_HPP
