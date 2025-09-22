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

#ifndef SERIAL_RUNNABLE_FACTORY_HPP
#define SERIAL_RUNNABLE_FACTORY_HPP

#include <runnables/api/serial_runnable.hpp>
#include <runnables/impl/serial_runnable_non_void.hpp>
#include <runnables/impl/serial_runnable_void.hpp>


namespace gempba {
    class serial_runnable_factory {
    public:
        serial_runnable_factory() = delete;

        serial_runnable_factory(const serial_runnable_factory &) = delete;

        // This is a factory for creating serial_runnable instances that wraps a void function
        struct return_none {
            template<typename... Args>
            static std::shared_ptr<serial_runnable> create(const int p_id, invokable<void, Args...> auto &&p_invokable,
                                                           std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer) {
                return std::make_shared<serial_runnable_void<void(Args...)> >(p_id, p_invokable, p_args_deserializer);
            }
        };

        // This is a factory for creating serial_runnable instances that wraps a non-void function
        struct return_value {
            template<typename R, typename... Args>
            static std::shared_ptr<serial_runnable> create(const int p_id, invokable<R, Args...> auto &&p_invokable,
                                                           std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer,
                                                           std::function<task_packet(R)> p_result_serializer) {
                return std::make_shared<serial_runnable_non_void<R(Args...)> >(p_id, p_invokable, p_args_deserializer, p_result_serializer);
            }
        };
    };
} // namespace gempba
#endif // SERIAL_RUNNABLE_FACTORY_HPP
