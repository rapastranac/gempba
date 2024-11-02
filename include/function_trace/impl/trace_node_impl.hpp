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

#ifndef GEMPBA_TRACE_NODE_IMPL_HPP
#define GEMPBA_TRACE_NODE_IMPL_HPP

#include <list>
#include <functional>
#include <iterator>
#include <variant>

#include "BranchHandler/BranchHandler.hpp"
#include "function_trace/api/abstract_trace_node.hpp"
#include "load_balancing/api/load_balancer.hpp"
#include "utils/utils.hpp"

#ifdef MULTIPROCESSING_ENABLED

#include <mpi.h>

#endif
/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    template<typename... Args>
    class TraceNodeImpl : public AbstractTraceNode<Args...> {
    private:
        TraceNode **_root = nullptr;                 // root of the tree
        TraceNode *_parent = nullptr;                // Parent node of this node (if any)
        TraceNode *_itself = nullptr;                // I don't remember why this is needed
        std::list<TraceNode *> _children;           // List of children nodes of this node (if any)

        std::variant<std::any, std::future<std::any>> _result;  // Result of the function call (if any)

        LoadBalancer &_dynamic_load_balancer;
        BranchHandler *_branch_handler = nullptr;

        std::tuple<Args...> _arguments;
        std::function<bool()> _branch_evaluator;
        TraceNodeState _state = UNUSED;
        int _forward_count = 0;
        int _push_count = 0;

        int _node_id = -1;
        int _thread_id = -1;
        int _depth = -1;
        bool _is_dummy = false;

        //<editor-fold desc="Multiprocessing member variables">
        int _destination_rank = -1;     // rank destination
        //</editor-fold>

        void init(int threadId, void *parent) {
            this->_thread_id = threadId;
            this->_node_id = _dynamic_load_balancer.getUniqueId();
            this->_itself = this;

            if (parent) {
                this->_root = _dynamic_load_balancer.getRoot(threadId);
                this->setParent(static_cast<TraceNode *>(parent));
            } else {
                // if there is no parent, it means the thread just took another subtree,
                // therefore, the root in handler.roots[threadId] should change since
                // no one else is supposed to be using it
                this->_dynamic_load_balancer.setRoot(threadId, this);
                this->_root = _dynamic_load_balancer.getRoot(threadId);
            }
        }

    public:
        explicit TraceNodeImpl(LoadBalancer &dynamicLoadBalancer, int threadId) : TraceNodeImpl(dynamicLoadBalancer, threadId, nullptr) {
            this->_is_dummy = true;
        }

        explicit TraceNodeImpl(LoadBalancer &dynamicLoadBalancer, int threadId, void *parent) : TraceNodeImpl(dynamicLoadBalancer, threadId, parent, nullptr) {}

        // this constructor is relevant when using multiprocessing
        explicit TraceNodeImpl(LoadBalancer &dynamicLoadBalancer, int threadId, void *parent, BranchHandler *branchHandler) : _dynamic_load_balancer(dynamicLoadBalancer),
                                                                                                                              _branch_handler(branchHandler) {
            this->init(threadId, parent);
        }

        bool operator==(const TraceNodeImpl &rhs) const {
            return _itself == rhs.getItself();
        }

        [[nodiscard]] bool isDummy() const override {
            return _is_dummy;
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
                default:
                    return;
            }
        }

        [[nodiscard]] TraceNodeState getState() const override {
            return this->_state;
        }

        [[nodiscard]] bool isResultReady() const override {
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

        [[nodiscard]] TraceNode *getItself() const override {
            return _itself;
        }

        void setRoot(TraceNode **root) override {
            if (_root != nullptr) {
                spdlog::debug("Node:{}, old root is {}\n", _node_id, static_cast<void *>(*_root));
            } else {
                spdlog::debug("Node:{}, old root is nullptr\n", _node_id);
            }
            if (root != nullptr) {
                spdlog::debug("Node:{}, new root to {}\n", _node_id, static_cast<void *>(*root));
            } else {
                spdlog::debug("Node:{}, new root is nullptr\n", _node_id);
            }
            if (_is_dummy && root != nullptr) {
                spdlog::throw_spdlog_ex("Cannot set root for a dummy node");
            }
            this->_root = root;
        }

        [[nodiscard]] TraceNode **getRoot() const override {
            return _root;
        }

        void setParent(TraceNode *parent) override {
            if (_is_dummy && parent != nullptr) {
                spdlog::throw_spdlog_ex("Cannot set parent for a dummy node");
            }
            if (parent == this) { // Check if the new parent is not this node itself
                spdlog::throw_spdlog_ex("Cannot set self as parent");
            }
            if (!_children.empty() && parent != nullptr) {
                spdlog::throw_spdlog_ex("Cannot set parent to nullptr when children are present");
            }
            if (_parent == parent) {
                // No change in parent, so return early to avoid recursion
                return;
            }

            this->_parent = parent;

            // Ensure the parent adds this node as a child if it's not already there
            if (_parent != nullptr) {
                this->_parent->addChild(this);
                this->setRoot(_parent->getRoot());
            }
        }

        [[nodiscard]] TraceNode *getParent() const override {
            return _parent;
        }

        TraceNode *getFirstChild() const override {
            return _children.empty() ? nullptr : _children.front();
        }

        TraceNode *getSecondChild() const override {
            if (_children.size() < 2) {
                spdlog::throw_spdlog_ex("Cannot get second child when there are less than 2 children");
            }
            auto it = _children.begin();
            return *(++it);
        }

        void pruneFrontChild() override {
            _children.pop_front();
        }

        void pruneSecondChild() override {
            if (_children.size() < 2) {
                spdlog::throw_spdlog_ex("Cannot prune second child when there are less than 2 children");
            }
            auto it = _children.begin();
            _children.erase(++it);
        }

        [[nodiscard]] TraceNode *getFirstSibling() const override {
            if (_is_dummy || _parent == nullptr) {
                return nullptr;
            }
            return _parent->getFirstChild();
        }

        [[nodiscard]] TraceNode *getPreviousSibling() const override {
            if (_is_dummy || _parent == nullptr) {
                return nullptr;
            }
            auto first = _parent->getChildren().begin();
            if (this == *first) {
                return nullptr;
            }
            auto it = std::find(first, _parent->getChildren().end(), this);
            return *(--it);
        }

        [[nodiscard]] TraceNode *getNextSibling() const override {
            if (_is_dummy || _parent == nullptr) {
                return nullptr;
            }
            auto last = _parent->getChildren().end();
            auto it = std::find(_parent->getChildren().begin(), last, this);
            if (this == *(--last)) {
                return nullptr;
            }
            TraceNode *temp = *(++it);
            return temp;
        }

        std::list<TraceNode *> &getChildren() override {
            return _children;
        }

        int getChildrenCount() const override {
            return _children.size();
        }

        void addChild(TraceNode *child) override {
            if (child == nullptr || child == this) {
                spdlog::throw_spdlog_ex("Cannot add null or self as a child");
            }

            // Set the parent of the child
            if (child->getParent() != this) {
                child->setParent(this);
            }

            // Add the child to this node's children list if not already added
            if (std::find(_children.begin(), _children.end(), child) == _children.end()) {
                this->_children.push_back(child);
            }
        }


        std::any getResult() override {
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

        void setResult(std::any &&result) override {
            this->_result = std::move(result);
        }

        void setFutureResult(std::future<std::any> &&future_result) override {
            this->_result = std::move(future_result);
        }

        //<editor-fold desc="Multiprocessing">
#ifdef MULTIPROCESSING_ENABLED

        void setDestinationRank(int destRank) override {
            this->_destination_rank = destRank;
        }

        std::string getSerializedResult() override {
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
            return ss.str();
        }

#endif
        //</editor-fold>
    };
}

#endif //GEMPBA_TRACE_NODE_IMPL_HPP
