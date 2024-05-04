#ifndef GEMPBA_RESULTAPI_HPP
#define GEMPBA_RESULTAPI_HPP

#include <any>
#include <future>
#include <list>
#include <utility>

#include "DLB/DLB_Handler.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
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
 * node, which means that it doesn't have arguments, or a real node, which means that it has arguments.
 *
 * A virtual node is a node that is created by the system to represent a function call that is not part of the trace tree, as a wau to
 * link the trace tree to the system.
 * @tparam Args
 */
    template<typename... Args>
    class TraceNode {

    public:
        TraceNode() = default;

        virtual ~TraceNode() = default;

        [[nodiscard]] virtual bool isVirtual() const = 0;

        [[nodiscard]] virtual int getThreadId() const = 0;

        [[nodiscard]] virtual int getNodeId() const = 0;

        /**
         * For debugging purposes only
         *
         * @return the number of times this node has been forwarded sequentially
         */
        [[nodiscard]] virtual int getForwardCount() const = 0;

        /**
         * For debugging purposes only
         * @return the number of times this node has been pushed to another thread
         */
        [[nodiscard]] virtual int getPushCount() const = 0;

        virtual void setArguments(Args &...args) final {
            setArguments(std::make_tuple(std::forward<Args &&>(args)...));
        }

        virtual void setArguments(std::tuple<Args &&...> args) = 0;

        virtual std::tuple<Args...> &getArgumentsRef() = 0;

        virtual std::tuple<Args...> getArgumentsCopy() = 0;

        virtual void setState(TraceNodeState state) = 0;

        virtual void setSentToAnotherProcess(int destRank) final {
            setState(SENT_TO_ANOTHER_PROCESS);
            _setDestinationRank(destRank);
        }

        [[nodiscard]] virtual TraceNodeState getState() const = 0;

        [[nodiscard]] virtual bool isConsumed() const = 0;

        [[nodiscard]] virtual bool isResultRetrievable() const = 0;

        template<typename T>
        void setResult(T &&result) {
            auto any = std::make_any<T>(std::forward<T>(result));
            _setAnyResult(std::move(any));
        }

        template<typename T>
        void setFutureResult(std::future<T> &&future) {
            std::future<std::any> anyFuture = std::async(std::launch::async, [_future = std::move(future)]() mutable {
                _future.wait();
                T result = _future.get();
                return std::make_any<T>(result);
            });

            _setFutureResult(std::move(anyFuture));
        }

        template<typename T>
        T get() {
            if (getState() == RETRIEVED) {
                throw std::runtime_error("Result already retrieved. Cannot retrieve it again.");
            }

            auto any = _getResult();
            try {
                return std::any_cast<T>(any);
            } catch (std::bad_any_cast &e) {
                throw std::runtime_error("Result is not of the expected type");
            }
        }

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

        /**
         * Get the TraceNode that represents the function call itself. This is useful when the TraceNode is a virtual
         * @return
         */
        virtual TraceNode<Args...> *getItself() const = 0;

        virtual void setRoot(void **root) = 0;

        [[nodiscard]] virtual void **getRoot() const = 0;

        /**
         * Set the parent of this node, if it is not a virtual node (i.e., it has arguments). If it is a virtual node,
         * it will throw a runtime_error exception. The root of the parent node will be set to the root of this node.
         */
        virtual void setParent(TraceNode<Args...> *parent) = 0;

        virtual TraceNode<Args...> *getParent() const = 0;

        virtual std::list<TraceNode<Args...> *> &getChildren() = 0;

    protected:

        virtual void _setDestinationRank(int destRank) = 0;

        /**
         * Sets the future result.
         *
         * When a task is submitted to the thread pool, it returns a future. This method is necessary
         * to obtain the result from the task, especially when the task no longer belongs to the thread
         * owning the instance of this node.
         *
         * @param result The future containing the result to be set.
         */
        virtual void _setFutureResult(std::future<std::any> &&result) = 0;

        virtual void _setAnyResult(std::any &&result) = 0;

        virtual std::any _getResult() = 0;

        //<editor-fold desc="Multiprocessing member functions">
    public:

        template<typename T, typename Deserializer>
        T get(Deserializer &deserializer) {
            if (getState() != SENT_TO_ANOTHER_PROCESS) {
                return get<T>();
            }
            auto ss = _getSerializedResult();
            T temp;
            deserializer(ss, temp);
            return temp;
        }

#ifdef MULTIPROCESSING_ENABLED
    protected:

/**
         * @note this function is not fully  developed yet. It is supposed to be used to retrieve the result from another
         * process. It is not being used in the current implementation. Also, only one thread per process is allowed to
         * use inter-process communication. Therefore, this function would have unexpected behavior if used in a multi-threaded
         * environment. Perhaps, the message retrieval should be done through the BranchHandler class, that has a reference to
         * the inter-process Scheduler.
         */
        virtual std::stringstream _getSerializedResult() = 0;

#endif
        //</editor-fold>
    };

}

#endif //GEMPBA_RESULTAPI_HPP
