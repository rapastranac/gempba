#ifndef TREE_HPP
#define TREE_HPP

#include "spdlog/spdlog.h"

#include <iostream>
#include <vector>

class Tree {
private:
    struct Node {
    private:
        Tree &_tree;
        Node *_parent;
        Node *_left_sibling;
        Node *_right_sibling;
        Node *_next;
        Node *_last;
        Node *_dummy;
        int _index = -1;
        int _children_count;

    public:

        explicit Node(Tree &tree, int index) : _tree(tree) {
            this->_index = index;
            this->_children_count = 0;
            this->_parent = nullptr;
            this->_left_sibling = nullptr;
            this->_right_sibling = nullptr;
            this->_next = nullptr;
            this->_last = nullptr;
            this->_dummy = nullptr;
        }

        /**
         * this method tells if this node is assigned to another one
         * @return
         */
        [[nodiscard]] bool isAssigned() const {
            return _parent ? true : false;
        }

        [[nodiscard]] bool hasNext() const {
            return _next ? true : false;
        }

        // this method returns the next node id, it returns -1 if not available
        [[nodiscard]] int getNext() const {
            if (_next) {
                return _next->_index;
            } else {
                return -1;
            }
        }

        [[nodiscard]] int getChildrenCount() const {
            return _children_count;
        }

        [[nodiscard]] int getParent() const {
            if (_parent) {
                return _parent->_index;
            } else {
                return -1;
            }
        }

        [[nodiscard]] int size() const {
            return _children_count;
        }

        /**
         * this method allows to add a next node
         *
         * @param index the index of the next node
         */
        void addNext(int index) {
            if (_tree[index]._parent != nullptr) {
                std::string errorMsg = "node " + std::to_string(index) + " is already assigned to " + std::to_string(_tree[index]._parent->_index) + "\n";
                spdlog::error(errorMsg);
                throw std::runtime_error(errorMsg);
            }

            _tree[index]._parent = this;
            if (_children_count == 0) {
                _next = &_tree[index];
            } else {
                Node *tail = _next;
                while (tail->_right_sibling) {
                    tail = tail->_right_sibling;
                }
                _tree[index]._left_sibling = tail;
                tail->_right_sibling = &_tree[index];
            }
            _last = &_tree[index];
            _children_count++;
        }

        /**
         * this method unlinks and pops out the next node, leaving the second one (if applicable) as the new next
         */
        void pop_front() {
            if (_next == nullptr) {
                std::string errorMsg = "there's no next to pop\n";
                spdlog::error(errorMsg);
                throw std::runtime_error(errorMsg);
            }

            if (_next->_right_sibling) {
                // at least two nodes
                Node *nextCpy = _next;
                _next = _next->_right_sibling;
                _next->_left_sibling = nullptr;

                nextCpy->_parent = nullptr;
                nextCpy->_left_sibling = nullptr;
                nextCpy->_right_sibling = nullptr;
            } else {
                // the only available node
                _next->_parent = nullptr;
                _next = nullptr;
                _last = nullptr;
            }
            --_children_count;
        }

        /**
         * This method clears the tree, removing all nodes
         */
        void clear() {
            while (_next) {
                pop_front();
            }
        }

        /**
         * releases current node from its parent
         */
        void release() {
            if (_parent == nullptr) {
                std::string errorMsg = "node " + std::to_string(_index) + " is not assigned to any other node \n";
                spdlog::error(errorMsg);
                throw std::runtime_error(errorMsg);
            }

            if (_left_sibling && _right_sibling) {
                // in the middle
                _left_sibling->_right_sibling = _right_sibling;
                _right_sibling->_left_sibling = _left_sibling;
                --(_parent->_children_count);
                _parent = nullptr;
                _right_sibling = nullptr;
                _left_sibling = nullptr;
            } else if (_left_sibling) {
                // last one
                _left_sibling->_right_sibling = nullptr;
                _parent->_last = _left_sibling;
                --(_parent->_children_count);
                _parent = nullptr;
                _left_sibling = nullptr;
            } else {
                // the only one or the first one
                _parent->pop_front();
            }

        }

        class Iterator {
        private:
            Node *node;
            friend struct Node;

            explicit Iterator(Node *node) : node(node) {}

        public:
            int &operator*() const {
                return node->_index;
            }

            // overload pre-increment operator
            Iterator &operator++() {
                node = node->_right_sibling;
                return *this;
            }

            // overload post-increment operator
            Iterator operator++(int) {
                Iterator ret = *this;
                ++*(this);
                return ret;
            }

            bool operator==(const Iterator &iter) const {
                return this->node == iter.node;
            }

            bool operator!=(const Iterator &iter) const {
                return this->node != iter.node;
            }
        };

        using iterator = Iterator;

        iterator begin() { return iterator(_next); }

        iterator end() { return iterator(_dummy); }
    };

    std::vector<Node> C;
public:
    Tree() = default;

    explicit Tree(int size) {
        C.reserve(size);
        for (int i = 0; i < size; i++) {
            C.emplace_back(*this, (int) i);
        }
    }

    [[nodiscard]] int size() const {
        return static_cast<int>(C.size());
    }

    void resize(int size) {
        C.reserve(size);
        for (int i = 0; i < size; i++) {
            C.emplace_back(*this, (int) i);
        }
    }

    Node &operator[](int index) {
        if (index >= size() || index < 0) {
            std::string errorMsg = "index " + std::to_string(index) + " is out of range \n";
            spdlog::error(errorMsg);
            throw std::range_error(errorMsg);
        }
        return C[index];
    }

    using iterator = std::vector<Node>::iterator;

    iterator begin() { return C.begin(); }

    iterator end() { return C.end(); }
};

#endif