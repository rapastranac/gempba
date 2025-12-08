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

#ifndef GEMPBA_SERIALIZABLE_HPP
#define GEMPBA_SERIALIZABLE_HPP

#include <gempba/utils/task_packet.hpp>

namespace gempba {

    class serializable {
    protected:
        serializable() = default;

    public:
        virtual ~serializable() = default;

        /**
         * Serialize the underlying task into bytes in the form of a task_packet
         * @return The serialized task as a task_packet
         */
        virtual task_packet serialize() = 0;

        /**
         * Deserialize the incoming bytes to the underlying task.
         * @param p_task The bytes to deserialize.
         */
        virtual void deserialize(const task_packet &p_task) = 0;
    };
}

#endif //GEMPBA_SERIALIZABLE_HPP
