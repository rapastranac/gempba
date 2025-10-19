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
#ifndef GEMPBA_STATS_HPP
#define GEMPBA_STATS_HPP

#include <any>
#include <functional>
#include <string>
#include <vector>
#include <gempba/utils/task_packet.hpp>

namespace gempba {
    class stats_visitor;

    class stats {

    protected:
        stats() = default;

    public:
        stats(const stats &) = default;

        virtual ~stats() = default;

        /**
         * Deserializes a stats object into a task_packet.
         * @return The bytes representing the serialized stats object.
         */
        [[nodiscard]] virtual task_packet serialize() const = 0;


        /**
         * Returns a vector of strings representing the labels of the internal members of the stats object.
         * @return A vector of strings with the labels of the stats members.
         */
        [[nodiscard]] virtual std::vector<std::string> labels() const =0;

        /**
         * This function allows to see the internal members of the stats object.
         * The visitor function should accept a string (the name of the member) and a std::any (the value of the member).
         * @param p_visitor A function that takes a string and a std::any, used to visit each member of the stats object.
         */
        virtual void visit(std::function<void(const std::string &, std::any &&)> p_visitor) const = 0;

        /**
         * Accepts a stats_visitor to visit the stats object.
         * @param p_visitor A pointer to a stats_visitor that will visit the stats object.
         */
        virtual void visit(stats_visitor *p_visitor) const = 0;
    };

}

#endif //GEMPBA_STATS_HPP
