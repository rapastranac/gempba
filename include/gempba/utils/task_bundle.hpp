/*
 * MIT License
 *
 * Copyright (c) 2025. Andrés Pastrana
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
#ifndef GEMPBA_TASK_BUNDLE_HPP
#define GEMPBA_TASK_BUNDLE_HPP
#include <gempba/utils/task_packet.hpp>

namespace gempba {
    class task_bundle {
        task_packet m_data;
        int m_function_id;

    public:
        // Explicit constructor
        explicit task_bundle(task_packet p_data, const int p_function_id) noexcept : m_data(std::move(p_data)), m_function_id(p_function_id) {}

        [[nodiscard]] task_packet get_task_packet() const noexcept {
            return m_data; // returns by value intentionally
        }

        [[nodiscard]] int get_runnable_id() const noexcept { return m_function_id; }

        [[nodiscard]] bool empty() const noexcept { return m_data.empty(); }

        [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }

        // Equality operators
        bool operator==(const task_bundle &p_other) const { return m_function_id == p_other.m_function_id && m_data == p_other.m_data; }

        bool operator!=(const task_bundle &p_other) const { return !(*this == p_other); }

        // Copy constructor and assignment
        task_bundle(const task_bundle &) = default;

        task_bundle &operator=(const task_bundle &) = default;

        // Move constructor and assignment
        task_bundle(task_bundle &&) noexcept = default;

        task_bundle &operator=(task_bundle &&) noexcept = default;

        // Default destructor
        ~task_bundle() = default;
    };
} // namespace gempba

#endif // GEMPBA_TASK_BUNDLE_HPP
