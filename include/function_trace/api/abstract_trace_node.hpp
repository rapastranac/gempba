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

#ifndef GEMPBA_RESULTAPI_HPP
#define GEMPBA_RESULTAPI_HPP

#include <any>
#include <future>
#include <list>
#include <string>
#include <utility>

#include "trace_node.hpp"
#include "utils/utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    /**
     * @brief This abstract class is used to represent a trace node that has arguments and a result.
     * @tparam Args the original arguments of the function
     */
    template<typename... Args>
    class AbstractTraceNode : public TraceNode {

    public:
        AbstractTraceNode() = default;

        ~AbstractTraceNode() override = default;

        virtual void setArguments(Args &...args) final {
            setArguments(std::make_tuple(std::forward<Args &&>(args)...));
        }

        virtual void setArguments(std::tuple<Args &&...> args) = 0;

        virtual std::tuple<Args...> &getArgumentsRef() = 0;

        virtual std::tuple<Args...> getArgumentsCopy() = 0;

    };

}

#endif //GEMPBA_RESULTAPI_HPP
