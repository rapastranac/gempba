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

#include <spdlog/spdlog.h>
#include <vector>

class tree {
    struct tree_node {
        explicit tree_node(tree &p_tree, const int p_index) : m_tree(p_tree) {
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
        void add_next(int p_index);

        // tell if this node is assigned to another one
        [[nodiscard]] bool is_assigned() const { return m_parent ? true : false; }

        [[nodiscard]] bool has_next() const { return m_next ? true : false; }

        // this method returns the next node id, it returns -1 if not available
        [[nodiscard]] int get_next() const { return m_next ? m_next->m_index : -1; }

        [[nodiscard]] int get_parent() const { return m_parent ? m_parent->m_index : -1; }

        // this method unlinks and pops out the next node, leaving the second one (if applicable) as the new next
        void pop_front();

        void clear() {
            while (m_next) {
                pop_front();
            }
        }

        // releases current node from its parent
        void release();

        [[nodiscard]] int size() const { return m_children_count; }

        class m_iterator {
            tree_node *m_node;

            friend struct tree_node;

            explicit m_iterator(tree_node *p_node) : m_node(p_node) {}

        public:
            int &operator*() const { return m_node->m_index; }

            // overload pre-increment operator
            m_iterator &operator++() {
                m_node = m_node->m_right_sibling;
                return *this;
            }

            // overload post-increment operator
            m_iterator operator++(int) {
                const m_iterator v_ret = *this;
                ++*(this);
                return v_ret;
            }

            bool operator==(const m_iterator &p_iter) const { return this->m_node == p_iter.m_node; }

            bool operator!=(const m_iterator &p_iter) const { return this->m_node != p_iter.m_node; }
        };

        using iterator = m_iterator;

        [[nodiscard]] iterator begin() const { return iterator(m_next); }

        [[nodiscard]] iterator end() const { return iterator(m_dummy); }

    private:
        tree &m_tree;
        tree_node *m_parent;
        tree_node *m_left_sibling;
        tree_node *m_right_sibling;
        tree_node *m_next;
        tree_node *m_last;
        tree_node *m_dummy;
        int m_index = -1;
        int m_children_count;
    };

    std::vector<tree_node> m_c;

public:
    tree() = default;

    // non-copyable
    tree(const tree &) = delete;

    tree &operator=(const tree &) = delete;

    // movable
    tree(tree &&) noexcept = default;

    tree &operator=(tree &&) noexcept = default;


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

    tree_node &operator[](const int p_idx) { return m_c[p_idx]; }

    using iterator = std::vector<tree_node>::iterator;

    iterator begin() { return m_c.begin(); }

    iterator end() { return m_c.end(); }

    [[nodiscard]] std::string to_string() const {
        std::ostringstream v_oss;
        for (size_t i = 0; i < m_c.size(); ++i) {
            if (m_c[i].get_parent() == -1) {
                print_node(v_oss, static_cast<int>(i), 0);
            }
        }
        return v_oss.str();
    }

private:
    void print_node(std::ostringstream &p_oss, const int p_index, const int p_indent) const {
        const auto &v_node = m_c[p_index];
        p_oss << std::string(p_indent * 2, ' ') << "- Node " << p_index << '\n';
        for (const int &v_index: v_node) {
            print_node(p_oss, v_index, p_indent + 1);
        }
    }
};

#endif
