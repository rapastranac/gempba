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

#ifndef RESULT_HPP
#define RESULT_HPP

#include <utility>

#include <utils/ipc/score.hpp>
#include <utils/ipc/task_packet.hpp>

namespace gempba {
    class result {
        result() :
            m_score(score::make(-1)), m_task_packet(task_packet::EMPTY) {
        }

        score m_score;
        task_packet m_task_packet;

    public:
        result(const int p_score, task_packet p_task_packet) :
            m_score(score::make(p_score)), m_task_packet(std::move(p_task_packet)) {
        }

        // Copy constructor
        result(const result &p_other) = default;

        // Move constructor
        result(result &&p_other) noexcept = default;

        // Copy assignment (deleted because of const member)
        result &operator=(const result &p_other) = default;

        // Move assignment (deleted because of const member)
        result &operator=(result &&p_other) noexcept = default;

        // Destructor
        ~result() = default;

        bool operator==(const result &p_other) const {
            return m_score == p_other.m_score && m_task_packet == p_other.m_task_packet;
        }

        bool operator!=(const result &p_other) const {
            return !(*this == p_other);
        }

        [[nodiscard]] int get_score_as_integer() const {
            return m_score.get_loose<int>();
        }

        [[nodiscard]] score get_score() const {
            return m_score;
        }

        [[nodiscard]] task_packet get_task_packet() const {
            return m_task_packet;
        }

        static const result EMPTY;
    };
} // namespace gempba

#endif // RESULT_HPP
