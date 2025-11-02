#include <gempba/utils/tree.hpp>
#include <gempba/utils/utils.hpp>

void tree::tree_node::add_next(const int p_index) {
    if (p_index >= static_cast<int>(m_tree.m_c.size())) {
        utils::log_and_throw("node " + std::to_string(p_index) + " is out of bounds");
    }
    if (m_tree[p_index].m_parent) {
        utils::log_and_throw("node " + std::to_string(m_index) + " is already assigned to " + std::to_string(m_tree[p_index].m_parent->m_index));
    }

    m_tree[p_index].m_parent = this;
    if (m_children_count == 0) {
        m_next = &m_tree[p_index];
    } else {
        tree_node *tail = m_next;
        while (tail->m_right_sibling) {
            tail = tail->m_right_sibling;
        }
        m_tree[p_index].m_left_sibling = tail;
        tail->m_right_sibling = &m_tree[p_index];
    }
    m_last = &m_tree[p_index];
    m_children_count++;
}

void tree::tree_node::pop_front() {
    if (m_next == nullptr) {
        utils::log_and_throw("there's no next to pop");
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

void tree::tree_node::release() {
    if (m_parent == nullptr) {
        utils::log_and_throw("node " + std::to_string(m_index) + " is not assigned to any other node");
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
