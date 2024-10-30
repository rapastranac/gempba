#ifndef GEMPBA_LOADBALANCER_HPP
#define GEMPBA_LOADBALANCER_HPP

#include "function_trace/api/trace_node.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-24
 */
namespace gempba {

    enum LoadBalancingStrategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };

    class LoadBalancer {
    protected:
        LoadBalancer() = default;

    public:
        static LoadBalancer &getInstance();

        virtual ~LoadBalancer() = default;

        virtual int getUniqueId() = 0;

        [[nodiscard]] virtual long long getIdleTime() const = 0;

        virtual void accumulateIdleTime(long nanoseconds) = 0;

        virtual void setRoot(int threadId, TraceNode *root) = 0;

        virtual TraceNode **getRoot(int threadId) = 0;

        virtual TraceNode *findTopTraceNode(TraceNode &node) = 0;

        virtual void maybePruneLeftSibling(TraceNode &node) = 0;

        virtual void pruneLeftSibling(TraceNode &node) = 0;

        virtual void prune(TraceNode &node) = 0;

        virtual void reset() = 0;

    };
}

#endif //GEMPBA_LOADBALANCER_HPP
