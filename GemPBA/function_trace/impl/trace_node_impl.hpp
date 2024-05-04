#ifndef GEMPBA_TRACE_NODE_IMPL_HPP
#define GEMPBA_TRACE_NODE_IMPL_HPP

#include <list>
#include <functional>
#include <variant>

#include "DLB/DLB_Handler.hpp"
#include "function_trace/api/trace_node.hpp"
#include "utils/utils.hpp"
#include "BranchHandler/BranchHandler.hpp"

#ifdef MULTIPROCESSING_ENABLED

#include <mpi.h>

#endif
/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    template<typename... Args>
    class TraceNodeImpl : public TraceNode<Args...> {
    private:
        void **_root = nullptr;                     // root of the tree
        TraceNode<Args...> *_parent = nullptr;      // Parent node of this node (if any)
        TraceNode<Args...> *_itself;                // I don't remember why this is needed
        std::list<TraceNode<Args...> *> _children;  // List of children nodes of this node (if any)

        std::variant<std::any, std::future<std::any>> _result;  // Result of the function call (if any)

        DLB_Handler &_dynamic_load_balancer;
        BranchHandler *_branch_handler = nullptr;

        std::tuple<Args...> _arguments;
        std::function<bool()> _branch_evaluator;
        TraceNodeState _state = UNUSED;
        int _forward_count = 0;
        int _push_count = 0;

        int _node_id = -1;
        int _thread_id = -1;
        int _depth = -1;
        bool _is_virtual = false;

        //<editor-fold desc="Multiprocessing member variables">
        int _destination_rank = -1;     // rank destination
        //</editor-fold>

        void init(int threadId, void *parent) {
            this->_thread_id = threadId;
            this->_node_id = _dynamic_load_balancer.getUniqueId();
            this->_itself = this;

            if (parent) {
                this->_root = _dynamic_load_balancer.getRoot(threadId);
                this->setParent(static_cast<TraceNode<Args...> *>(parent));
            } else {
                // if there is no parent, it means the thread just took another subtree,
                // therefore, the root in handler.roots[threadId] should change since
                // no one else is supposed to be using it
                this->_dynamic_load_balancer.assign_root(threadId, this);
                this->_root = _dynamic_load_balancer.getRoot(threadId);
            }
        }

    public:
        explicit TraceNodeImpl(DLB_Handler &dynamicLoadBalancer, int threadId) : TraceNodeImpl(dynamicLoadBalancer, threadId, nullptr) {
            this->_is_virtual = true;
        }

        explicit TraceNodeImpl(DLB_Handler &dynamicLoadBalancer, int threadId, void *parent) : TraceNodeImpl(dynamicLoadBalancer, threadId, parent, nullptr) {}

        // this constructor is relevant when using multiprocessing
        explicit TraceNodeImpl(DLB_Handler &dynamicLoadBalancer, int threadId, void *parent, BranchHandler *branchHandler) : _dynamic_load_balancer(dynamicLoadBalancer),
                                                                                                                             _branch_handler(branchHandler) {
            this->init(threadId, parent);
        }

        [[nodiscard]] bool isVirtual() const override {
            return _is_virtual;
        }

        [[nodiscard]] int getThreadId() const override {
            return _thread_id;
        }

        [[nodiscard]] int getNodeId() const override {
            return _node_id;
        }

        [[nodiscard]] int getForwardCount() const override {
            return _forward_count;
        }

        [[nodiscard]] int getPushCount() const override {
            return _push_count;
        }

        void setArguments(std::tuple<Args &&...> args) override {
            this->_arguments = std::move(args);
        }

        std::tuple<Args...> &getArgumentsRef() override {
            return _arguments;
        }

        std::tuple<Args...> getArgumentsCopy() override {
            return _arguments;
        }

        void setState(TraceNodeState state) override {
            this->_state = state;
            switch (_state) {
                case FORWARDED: {
                    this->_forward_count++;
                    break;
                }
                case PUSHED: {
                    this->_push_count++;
                    break;
                }
            }
        }

        [[nodiscard]] TraceNodeState getState() const override {
            return this->_state;
        }

        [[nodiscard]] bool isResultRetrievable() const override {
            return this->_state != DISCARDED && this->_state != RETRIEVED;
        }

        [[nodiscard]] bool isConsumed() const override {
            return _state != UNUSED;
        }

        void setBranchEvaluator(std::function<bool()> &&branchEvaluator) override {
            this->_branch_evaluator = [Func = std::forward<std::function<bool()>>(branchEvaluator)] { return Func(); };
        }

        bool isBranchWorthExploring() override {
            if (_state == FORWARDED || _state == PUSHED || _state == DISCARDED) {
                return false;
            }
            return this->_branch_evaluator();
        }

        ~TraceNodeImpl() = default;

        TraceNode<Args...> *getItself() const override {
            return _itself;
        }

        void setRoot(void **root) override {
            printf("Node:%d, old root is %p\n", _node_id, *this->_root);
            printf("Node:%d, new root to %p\n", _node_id, *root);
            this->_root = root;
        }

        [[nodiscard]] void **getRoot() const override {
            return _root;
        }

        void setParent(TraceNode<Args...> *parent) override {
            if (this->isVirtual()) {
                throw std::runtime_error("Cannot set parent for a virtual node");
            }
            if (parent) {
                this->_parent = parent;
                this->_parent->getChildren().push_back(this);
                this->setRoot(parent->getRoot());

                return;
            }
            if (!this->_children.empty()) {
                throw std::runtime_error("Cannot set parent to nullptr when children are present");
            }
            this->_parent = nullptr;
        }

        TraceNode<Args...> *getParent() const override {
            return _parent;
        }

        std::list<TraceNode<Args...> *> &getChildren() override {
            return _children;
        }

        bool operator==(const TraceNodeImpl &rhs) const {
            return _itself == rhs.getItself();
        }

    protected:

        void _setDestinationRank(int destRank) override {
            this->_destination_rank = destRank;
        }

        void _setFutureResult(std::future<std::any> &&result) override {
            this->_result = std::move(result);
        }

        void _setAnyResult(std::any &&result) override {
            this->_result = std::move(result);
        }

        std::any _getResult() override {
            if (std::holds_alternative<std::any>(_result)) {
                this->_state = RETRIEVED;
                return std::get<std::any>(_result);
            }
            auto &future = std::get<std::future<std::any>>(_result);
            future.wait();
            std::any anyResult = future.get();
            this->_state = RETRIEVED;
            return anyResult;
        }

        //<editor-fold desc="Multiprocessing">
#ifdef MULTIPROCESSING_ENABLED

        std::stringstream _getSerializedResult() override {
            /** this blocks any other thread to use a multiprocessing function since MPI_Recv is blocking thus, mpi_thread_serialized is guaranteed */
            this->_branch_handler->lock();

            utils::print_mpi_debug_comments("rank {} entered get() to retrieve from {}! \n", this->_branch_handler->getWorldRank(), this->_destination_rank);

            MPI_Status status;
            // The scheduler should be the one providing the communicator
            MPI_Probe(this->_destination_rank, MPI::ANY_TAG, MPI_COMM_WORLD, &status); // receives status before receiving the message
            int bytes;
            MPI_Get_count(&status, MPI::CHAR, &bytes); // receives total number of datatype elements of the message

            std::unique_ptr<char[]> in_buffer(new char[bytes]);
            MPI_Recv(in_buffer.get(), bytes, MPI::CHAR, this->_destination_rank, MPI::ANY_TAG, MPI_COMM_WORLD, &status);

            utils::print_mpi_debug_comments("rank {} received {} Bytes from {}! \n", this->_branch_handler->getWorldRank(), bytes, this->_destination_rank);

            std::stringstream ss;
            ss.write(in_buffer.get(), bytes);
            this->_branch_handler->unlock(); /* release mpi mutex; thus, other threads are able to push to other nodes*/
            return ss;
        }

#endif
        //</editor-fold>
    };
}

#endif //GEMPBA_TRACE_NODE_IMPL_HPP
