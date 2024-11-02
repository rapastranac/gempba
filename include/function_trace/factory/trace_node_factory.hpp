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

#ifndef GEMPBA_TRACE_NODE_FACTORY_HPP
#define GEMPBA_TRACE_NODE_FACTORY_HPP

#include <memory>
#include <utility>

#include "load_balancing/api/load_balancer.hpp"
#include "function_trace/impl/trace_node_impl.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class TraceNodeFactory {
    public:

        /**
         * This factory method creates a TraceNode multithreading capable only
         */
        template<typename... Args>
        static std::shared_ptr<TraceNode> createNode(LoadBalancer &dynamicLoadBalancer, int threadId, void *parent) {
            return std::make_shared<TraceNodeImpl<Args...>>(dynamicLoadBalancer, threadId, parent);
        }

        /**
         * This factory method creates a TraceNode multiprocessing capable
         * @param branchHandler reference to BranchHandler
         */
        template<typename... Args>
        static std::shared_ptr<TraceNode> createNode(BranchHandler &branchHandler, LoadBalancer &dynamicLoadBalancer, int threadId, void *parent) {
            return std::make_shared<TraceNodeImpl<Args...>>(TraceNodeImpl<Args...>(dynamicLoadBalancer, threadId, parent, &branchHandler));
        }

        /**
         *  This factory method creates a dummy TraceNode that will serve as a parent of other TraceNode instances. As this
         *  dummy TraceNode is only an anchor, and does not contain arguments, it will work for either multithreading or
         *  multiprocessing purposes
         */
        static std::shared_ptr<TraceNode> createDummy(LoadBalancer &dynamicLoadBalancer, int threadId) {
            return std::make_shared<TraceNodeImpl<>>(dynamicLoadBalancer, threadId);
        }

    };

}
#endif //GEMPBA_TRACE_NODE_FACTORY_HPP
