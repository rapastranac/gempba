/*
 * MIT License
 *
 * Copyright (c) 2021. Andrés Pastrana
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
#ifndef TREE_HPP
#define TREE_HPP

#include <spdlog/common.h>
#include <vector>

class tree {
    struct tree_node {
        explicit tree_node(tree& p_tree, const int p_index) :
            m_tree(p_tree) {
            this->m_index = p_index;
            this->m_children_count = 0;
            this->m_parent = nullptr;
            this->m_left_sibling = nullptr;
            this->m_right_sibling = nullptr;
            this->m_next = nullptr;
            this->m_last = nullptr;
            this->m_dummy = nullptr;
        }

        // this method allows to add a next node,
        void add_next(const int p_index) {
            if (m_tree[p_index].m_parent) {
                spdlog::throw_spdlog_ex("node " + std::to_string(m_index) + " is already assigned to " + std::to_string(m_tree[p_index].m_parent->m_index));
            }

            m_tree[p_index].m_parent = this;
            if (m_children_count == 0) {
                m_next = &m_tree[p_index];
            } else {
                tree_node* tail = m_next;
                while (tail->m_right_sibling) {
                    tail = tail->m_right_sibling;
                }
                m_tree[p_index].m_left_sibling = tail;
                tail->m_right_sibling = &m_tree[p_index];
            }
            m_last = &m_tree[p_index];
            m_children_count++;
        }

        // tell if this node is assigned to another one
        [[nodiscard]] bool is_assigned() const { return m_parent ? true : false; }

        [[nodiscard]] bool has_next() const { return m_next ? true : false; }

        // this method returns the next node id, it returns -1 if not available
        [[nodiscard]] int get_next() const { return m_next ? m_next->m_index : -1; }

        [[nodiscard]] int get_parent() const { return m_parent ? m_parent->m_index : -1; }

        // this method unlinks and pops out the next node, leaving the second one (if applicable) as the new next
        void pop_front() {
            if (m_next == nullptr) {
                spdlog::throw_spdlog_ex("there's no next to pop");
            }
            if (m_next->m_right_sibling != nullptr) {
                // at least two nodes
                const auto next_cpy = m_next;
                m_next = m_next->m_right_sibling;
                m_next->m_left_sibling = nullptr;

                next_cpy->m_parent = nullptr;
                next_cpy->m_left_sibling = nullptr;
                next_cpy->m_right_sibling = nullptr;
            } else {
                // the only available node
                m_next->m_parent = nullptr;
                m_next = nullptr;
                m_last = nullptr;
            }
            --m_children_count;
        }

        void clear() {
            while (m_next) {
                pop_front();
            }
        }

        // releases current node from its parent
        void release() {
            if (m_parent == nullptr) {
                spdlog::throw_spdlog_ex("node " + std::to_string(m_index) + " is not assigned to any other node");
            }

            if (m_left_sibling && m_right_sibling) {
                // both non-nullptr
                // in the middle
                m_left_sibling->m_right_sibling = m_right_sibling;
                m_right_sibling->m_left_sibling = m_left_sibling;
                --(m_parent->m_children_count);
                m_parent = nullptr;
                m_right_sibling = nullptr;
                m_left_sibling = nullptr;
            } else if (m_left_sibling && !m_right_sibling) {
                // left non-null and right null
                // last one
                m_left_sibling->m_right_sibling = nullptr;
                m_parent->m_last = m_left_sibling;
                --(m_parent->m_children_count);
                m_parent = nullptr;
                m_left_sibling = nullptr;
            } else {
                // (!m_left_sibling && m_right_sibling) : left null and right non-null
                // the only one or the first one
                m_parent->pop_front();
            }
        }

        [[nodiscard]] int size() const { return m_children_count; }

        class m_iterator {
            tree_node* m_node;

            friend struct tree_node;

            explicit m_iterator(tree_node* p_node) :
                m_node(p_node) {
            }

        public:
            int& operator*() const { return m_node->m_index; }

            // overload pre-increment operator
            m_iterator& operator++() {
                m_node = m_node->m_right_sibling;
                return *this;
            }

            // overload post-increment operator
            m_iterator operator++(int) {
                const m_iterator ret = *this;
                ++*(this);
                return ret;
            }

            bool operator==(const m_iterator& p_iter) const { return this->m_node == p_iter.m_node; }

            bool operator!=(const m_iterator& p_iter) const { return this->m_node != p_iter.m_node; }
        };

        using iterator = m_iterator;

        [[nodiscard]] iterator begin() const { return iterator(m_next); }

        [[nodiscard]] iterator end() const { return iterator(m_dummy); }

    private:
        tree& m_tree;
        tree_node* m_parent;
        tree_node* m_left_sibling;
        tree_node* m_right_sibling;
        tree_node* m_next;
        tree_node* m_last;
        tree_node* m_dummy;
        int m_index = -1;
        int m_children_count;
    };

    std::vector<tree_node> m_c;

public:
    tree() = default;

    // non-copyable
    tree(const tree&) = delete;
    tree& operator=(const tree&) = delete;

    // movable
    tree(tree&&) noexcept = default;
    tree& operator=(tree&&) noexcept = default;


    explicit tree(const size_t p_size) {
        for (size_t i = 0; i < p_size; i++) {
            m_c.emplace_back(*this, static_cast<int>(i));
        }
    }

    void resize(const size_t p_size) {
        for (size_t i = 0; i < p_size; i++) {
            m_c.emplace_back(*this, static_cast<int>(i));
        }
    }

    [[nodiscard]] size_t size() const { return m_c.size(); }

    tree_node& operator[](const int p_idx) { return m_c[p_idx]; }

    using iterator = std::vector<tree_node>::iterator;

    iterator begin() { return m_c.begin(); }

    iterator end() { return m_c.end(); }
};

#endif
