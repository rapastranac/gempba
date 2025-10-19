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

#ifndef SERIAL_RUNNABLE_VOID_HPP
#define SERIAL_RUNNABLE_VOID_HPP

#include <functional>
#include <future>
#include <optional>

#include <branch_handling/branch_handler.hpp>
#include <gempba/core/node.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/utils/task_packet.hpp>

namespace gempba {
    template<typename T>
    class serial_runnable_void;

    template<typename... Args>
    class serial_runnable_void<void(Args...)> final : public serial_runnable {
        const int m_id;
        std::function<void(std::thread::id, Args..., node)> m_f;
        std::function<std::tuple<Args...>(const task_packet &&)> m_args_deserializer;

    public:
        serial_runnable_void(const int p_id, invokable<void, Args...> auto &&p_f, std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer) :
            m_id(p_id), m_f(p_f), m_args_deserializer(p_args_deserializer) {
        }

        ~serial_runnable_void() override = default;

        [[nodiscard]] int get_id() const override { return m_id; }

        std::optional<std::shared_future<task_packet> > operator()(branch_handler &p_branch_handler, const task_packet &p_args) override {
            std::tuple<Args...> v_user_args = m_args_deserializer(std::move(p_args));
            std::future<std::any> v_fut = p_branch_handler.force_local_submit([this, v_tup = std::move(v_user_args)] {
                auto v_all_args = std::tuple_cat(std::make_tuple(std::this_thread::get_id()), v_tup, std::make_tuple<node>(node()));
                std::apply(m_f, v_all_args);
                return std::any{};
            });
            // fut is ignored

            return std::nullopt;
        }
    };
} // namespace gempba


#endif // SERIAL_RUNNABLE_VOID_HPP
