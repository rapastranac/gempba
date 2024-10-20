#ifndef GEMPBA_TRACE_NODE_HPP
#define GEMPBA_TRACE_NODE_HPP

#include "utils/utils.hpp"
#include <string>
#include <any>
#include <future>
#include <list>
#include <utility>

/**
 * @author Andres Pastrana
 * @date 2024-08-25
 */
namespace gempba {

    enum TraceNodeState {
        UNUSED,                 // Node hasn't been used yet
        FORWARDED,              // Data is forwarded within the system
        PUSHED,                 // Data is pushed to another thread within the same process
        DISCARDED,              // Data is discarded within the system
        RETRIEVED,              // Result is retrieved from the data
        SENT_TO_ANOTHER_PROCESS // Data is sent to another process
    };

    /**
     * @brief Interface for a node in the trace tree. This node represents the top of a function call. It can be a virtual
     * node, which means that it doesn't wrap any arguments, or a real node, which means that it wraps arguments.
     *
     * A dummy node is a node that is created by the system to represent a function call that is not part of the trace tree, as a wau to
     * link the trace tree to the system.
     */
    class TraceNode {
    public:
        TraceNode() = default;

        virtual ~TraceNode() = default;

        /**
         * Get the TraceNode that represents the function call itself. This is useful when the TraceNode is a virtual
         * @return
         */
        [[nodiscard]] virtual TraceNode *getItself() const = 0;

        [[nodiscard]] virtual bool isDummy() const = 0;

        /**
         * Get the thread id that initialized this node. This is the thread that is executing the function call.
         * @return thread id
         */
        [[nodiscard]] virtual int getThreadId() const = 0;

        /**
         * For debugging purposes only. This is the unique identifier of the node, this is truly unique within the thread
         * and it is not shared between threads.
         * @return the unique identifier of the node
         */
        [[nodiscard]] virtual int getNodeId() const = 0;

        /**
       * For debugging purposes only. It should not greater than one.
       *
       * @return the number of times this node has been forwarded sequentially
       */
        [[nodiscard]] virtual int getForwardCount() const = 0;

        /**
         * For debugging purposes only. It should not greater than one.
         * @return the number of times this node has been pushed to another thread
         */
        [[nodiscard]] virtual int getPushCount() const = 0;

        [[nodiscard]] virtual TraceNodeState getState() const = 0;

        virtual void setState(TraceNodeState state) = 0;

        [[nodiscard]] virtual bool isConsumed() const = 0;

        /**
         * Sets the root of the tree. All children will be set to the root of this node.
         * @param root the root of the tree
         */
        virtual void setRoot(TraceNode **root) = 0;

        [[nodiscard]] virtual TraceNode **getRoot() const = 0;

        /**
         * Set the parent of this node, if it is not a dummy (i.e., it has arguments). If it is a dummy,
         * it will throw a runtime_error exception. The root of the parent node will be set to the root of this node.
         */
        virtual void setParent(TraceNode *parent) = 0;

        /**
         * Get the parent of this node, if it is not a dummy (i.e., it has arguments). If it is a dummy,
         * it will return nullptr.
         * @return the parent of this node, nullptr otherwise
         */
        [[nodiscard]] virtual TraceNode *getParent() const = 0;

        /**
         * The the first child of this node, which is also the <code>leftMost</code> child.
         * @return the first child of this node
         */
        [[nodiscard]] virtual TraceNode *getFirstChild() const = 0;

        /**
         * The the second child of this node.
         * @return the second child of this node
         */
        [[nodiscard]] virtual TraceNode *getSecondChild() const = 0;

        /**
         * Removes the first child of this node.
         */
        virtual void pruneFrontChild() = 0;

        /**
         * Removes the second child of this node. If the node has only one child, it will throw a runtime_error exception.
         */
        virtual void pruneSecondChild() = 0;

        /**
         * If this node if the <code>leftMost</code> child of its parent node, it will return itself. Otherwise,
         * it will return the <code>leftMost</code> child of its parent node. If this node is a dummy, it will return nullptr.
         * @return the first sibling of this node
         */
        [[nodiscard]] virtual TraceNode *getFirstSibling() const = 0;

        /**
         * It will return the second child of its parent parent node. If this node is a dummy, it will return nullptr.
         * If this node is the first child of its parent, it will return nullptr.
         * @return previous sibling of this node
         */
        [[nodiscard]] virtual TraceNode *getPreviousSibling() const = 0;

        /**
         * It will return the next sibling of this node. If this node is the last child of its parent, it will return nullptr.
         * If this node is a dummy, it will return nullptr.
         * @return next sibling of this node
         */
        [[nodiscard]] virtual TraceNode *getNextSibling() const = 0;

        [[deprecated("Internal use only")]] virtual std::list<TraceNode *> &getChildren() = 0;

        virtual int getChildrenCount() const = 0;

        virtual void addChild(TraceNode *child) = 0;

        virtual std::any getResult() = 0;

        virtual void setResult(std::any &&result) = 0;

        [[nodiscard]] virtual bool isResultReady() const = 0;

        /**
        * Sets the future result.
        *
        * When a task is submitted to the thread pool, it returns a future. This method is necessary
        * to obtain the result from the task, especially when the task no longer belongs to the thread
        * owning the instance of this node.
        *
        * @param result The future containing the result to be set.
        */
        virtual void setFutureResult(std::future<std::any> &&future_result) = 0;

        /**
        * As this is meant to keep track of a recursive function call, the TraceNode could have been enqueued
        * to be processed by another thread. This method allows to pass a function that evaluate the current
        * state of the system to decide if the branch should be explored or not.
        * @param branchEvaluator
        */
        virtual void setBranchEvaluator(std::function<bool()> &&branchEvaluator) = 0;

        /**
         * This should be invoked always before calling a branch, since
         * it invokes user's instructions to prepare data that will be pushed
         * If not invoked, input for a specific branch handled by ResultHolder instance
         * will be empty.
         *
         * This method allows always having input data ready before a branch call, avoiding having
         * data in the stack before it is actually needed.
         *
         * Thus, the user can evaluate any condition to check if a branch call is worth it or
         * not, while creating temporarily an input data set.
         *
         * If user's condition is met, then this temporarily input is held by the ResultHolder::holdArgs(...)
         * and it should return true
         * If user's condition is not met, no input is held, and it should return false
         *
         * If a void function is being used, this should be a problem, since
         *
         * @return true if the branch represented by this TraceNode is worth exploring, false otherwise
         */
        virtual bool isBranchWorthExploring() = 0;

        //<editor-fold desc="Multiprocessing member functions">
#ifdef MULTIPROCESSING_ENABLED

        /**
         * If the arguments wrapped by this TraceNode are sent to another process, this function should be called to
         * indicate the rank of the process that will receive the arguments, so that the result can be sent back to the
         * caller process.
         * @param destRank The rank of the process that will receive the arguments.
         */
        [[maybe_unused]] virtual void setDestinationRank(int destRank) = 0;

        /**
            * @note this function is not fully  developed yet. It is supposed to be used to retrieve the result from another
            * process. It is not being used in the current implementation. Also, only one thread per process is allowed to
            * use inter-process communication. Therefore, this function would have unexpected behavior if used in a multi-threaded
            * environment. Perhaps, the message retrieval should be done through the BranchHandler class, that has a reference to
            * the inter-process Scheduler.
            */
        [[maybe_unused]] virtual std::string getSerializedResult() = 0;

#endif
        //</editor-fold>
    };
}

#endif //GEMPBA_TRACE_NODE_HPP
