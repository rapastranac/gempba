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
