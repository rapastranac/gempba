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
#ifndef SERIAL_RUNNABLE_HPP
#define SERIAL_RUNNABLE_HPP

#include <future>
#include <optional>

#include <gempba/utils/task_packet.hpp>

namespace gempba {
    class branch_handler;

    class serial_runnable {
    protected:
        serial_runnable() = default;

    public:
        virtual ~serial_runnable() = default;

        [[nodiscard]] virtual int get_id() const = 0;

        virtual std::optional<std::shared_future<task_packet> > operator()(branch_handler &p_node_manager, const task_packet &p_task) = 0;
    };
} // namespace gempba

#endif // SERIAL_RUNNABLE_HPP
