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

#ifndef GEMPBA_SCHEDULER_TRAITS_HPP
#define GEMPBA_SCHEDULER_TRAITS_HPP

#include <memory>

/**
 * @author Andres Pastrana
 * @date 2025-06-28
 */
namespace gempba {

    class stats;

    class scheduler_traits {
    protected:
        scheduler_traits() = default;

    public:
        virtual ~scheduler_traits() = default;

        /**
         * Synchronize all processes in the distributed system.
         */
        virtual void barrier() = 0;

        /**
         * @return  The rank of the current process in the world communicator.
         */
        [[nodiscard]] virtual int rank_me() const = 0;

        /**
         * @return  The number of processes in the world communicator.
         */
        [[nodiscard]] virtual int world_size() const = 0;

        /**
         * Get the statistics of the scheduler at the current process as a unique pointer.
         *
         * @return A unique pointer to the stats object.
         */
        [[nodiscard]] virtual std::unique_ptr<stats> get_stats() const = 0;
    };

} // namespace gempba

#endif // GEMPBA_SCHEDULER_TRAITS_HPP
