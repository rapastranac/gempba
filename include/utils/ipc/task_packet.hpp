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

#ifndef TASK_PACKET_HPP
#define TASK_PACKET_HPP

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace gempba {
    // Requires: C++17 or later
    class task_packet {
        std::vector<std::byte> m_data;

        task_packet() = default;

    public:
        // Construct from std::vector<byte> (copy)
        explicit task_packet(const std::vector<std::byte> &p_data) :
            task_packet(std::vector(p_data)) {
        }

        // Construct from std::vector<byte> (move)
        explicit task_packet(std::vector<std::byte> &&p_data) noexcept :
            m_data(std::move(p_data)) {
        }

        // Construct from size only (uninitialized)
        explicit task_packet(const std::size_t p_size) :
            m_data(p_size) {
        }

        // Construct from const char* + size (copy)
        task_packet(const char *p_buffer, const std::size_t p_size) :
            task_packet(p_size) {
            if (p_size > 0 && !p_buffer) {
                throw std::invalid_argument("task_packet: null buffer with non-zero size");
            }
            std::memcpy(m_data.data(), p_buffer, p_size);
        }

        // Construct from std::string (copy)
        explicit task_packet(const std::string &p_str) :
            task_packet(p_str.data(), p_str.size()) {
        }

        // Access the raw buffer
        std::byte *data() noexcept { return m_data.data(); }
        [[nodiscard]] const std::byte *data() const noexcept { return m_data.data(); }

        [[nodiscard]] std::size_t size() const noexcept { return m_data.size(); }

        [[nodiscard]] bool empty() const noexcept {
            return m_data.empty();
        }

        // Allow default copy constructor
        task_packet(const task_packet &) = default;

        task_packet &operator=(const task_packet &) = default;

        // Allow move semantics
        task_packet(task_packet &&) noexcept = default;

        task_packet &operator=(task_packet &&) noexcept = default;

        [[nodiscard]] auto begin() const noexcept {
            return std::cbegin(m_data);
        }

        [[nodiscard]] auto end() const noexcept {
            return std::cend(m_data);
        }

        bool operator==(const task_packet &p_other) const {
            return m_data == p_other.m_data;
        }

        bool operator!=(const task_packet &p_other) const {
            return !(*this == p_other);
        }

        // Singleton EMPTY instance
        const static task_packet EMPTY;
    };
}

#endif //TASK_PACKET_HPP
