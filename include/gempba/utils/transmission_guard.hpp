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
#ifndef GEMPBA_TRANSMISSION_GUARD_HPP
#define GEMPBA_TRANSMISSION_GUARD_HPP
#include <mutex>

namespace gempba {

    class transmission_guard final {
        std::unique_lock<std::mutex> m_lock;

    public:
        // Construct by taking ownership of a unique_lock
        explicit transmission_guard(std::unique_lock<std::mutex> &&p_lock) :
            m_lock(std::move(p_lock)) {
        }

        // Disable everything: no copying, no assignment
        transmission_guard(const transmission_guard &) = delete;

        transmission_guard &operator=(const transmission_guard &) = delete;

        // Allow move semantics
        transmission_guard(transmission_guard &&) = default;

        transmission_guard &operator=(transmission_guard &&) = default;

        // Destructor automatically unlocks the mutex
        ~transmission_guard() = default;
    };


}

#endif //GEMPBA_TRANSMISSION_GUARD_HPP
