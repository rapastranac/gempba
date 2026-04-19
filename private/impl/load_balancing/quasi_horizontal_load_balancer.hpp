/*
 * MIT License
 *
 * Copyright (c) 2025. Andrés Pastrana
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

#ifndef GEMPBA_QUASI_HORIZONTAL_LOAD_BALANCER_HPP
#define GEMPBA_QUASI_HORIZONTAL_LOAD_BALANCER_HPP

#include <BS_thread_pool.hpp>
#include <set>
#include <thread>

#include <gempba/core/load_balancer.hpp>
#include <gempba/core/node.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/utils.hpp>

/**
 * @author Andres Pastrana
 * @date 2025-08-30
 */

namespace gempba {
    class quasi_horizontal_load_balancer final : public load_balancer {

        BS::thread_pool<> m_thread_pool;
        unsigned int m_unique_id_counter = 0;
        std::recursive_mutex m_recursive_mutex;
        scheduler::worker *const m_scheduler_worker;
        std::map<std::thread::id, std::shared_ptr<std::shared_ptr<node_core>>> m_roots; // every thread will be solving a subtree, this point to their roots
        unsigned int m_thread_count = std::thread::hardware_concurrency();
        std::size_t m_thread_request_count = 0;

    public:
        explicit quasi_horizontal_load_balancer(scheduler::worker *const p_scheduler_worker = nullptr) : m_scheduler_worker(p_scheduler_worker) {}

        balancing_policy get_balancing_policy() override { return QUASI_HORIZONTAL; }

        unsigned generate_unique_id() override { return ++m_unique_id_counter; }

        std::future<std::any> force_local_submit(std::function<std::any()> &&p_function) override {
            ++m_thread_request_count;
            return m_thread_pool.submit_task(p_function);
        }

        void prune_and_correct_root_after_forward(node &p_node) {
            node v_parent = p_node.get_parent();
            if (v_parent == nullptr) {
                // p_node is a root (or was just made root), just prune it
                p_node.prune();
                return;
            }
            p_node.prune(); // prune as it was already resolved
            try_correct_root(v_parent);
        }

        void forward(node &p_node) override {
            if (p_node.get_state() == UNUSED && p_node.should_branch()) {
                p_node.run();
            }
            prune_and_correct_root_after_forward(p_node);
        }

        bool try_local_submit(node &p_node) override {
            if (send(p_node)) {
                return true;
            }
            forward(p_node);
            return false;
        }

        bool try_remote_submit(node &p_node, const int p_runnable_id) override {
            if (!m_scheduler_worker) {
                utils::log_and_throw("Attempted to do remote submission without a scheduler worker");
            }
            if (send(p_node, p_runnable_id)) {
                return true;
            }
            try_local_submit(p_node);
            return false;
        }

        [[nodiscard]] double get_idle_time() const override { return m_thread_pool.get_wall_idle_time(); }

        void set_root(const std::thread::id p_thread_id, std::shared_ptr<node_core> &p_root) override {
            std::scoped_lock v_lock(m_recursive_mutex);
            if (m_roots.contains(p_thread_id)) {
                const auto v_ptr = m_roots.at(p_thread_id);
                *v_ptr = p_root; // should I assign this way
            } else {
                m_roots.emplace(p_thread_id, std::make_shared<std::shared_ptr<node_core>>(p_root));
            }
        }

        std::shared_ptr<std::shared_ptr<node_core>> get_root(const std::thread::id p_thread_id) override {
            std::scoped_lock v_lock(m_recursive_mutex);
            if (m_roots.contains(p_thread_id)) {
                return m_roots.at(p_thread_id);
            }
            return nullptr;
        }

        void set_thread_pool_size(const unsigned p_size) override {
            m_thread_pool.reset(p_size);
            spdlog::debug("thread pool size: {}", m_thread_pool.get_thread_count());
        }

        void wait() override { m_thread_pool.wait(); }

        bool is_done() const override { return m_thread_pool.get_tasks_total() == 0; }

        std::size_t get_thread_request_count() const override { return m_thread_request_count; }

#if GEMPBA_DEV_MODE && GEMPBA_BUILD_TESTS

    public:
#else
    private:
#endif


        /**
         * @brief Utility method to check if the thread_pool can receive another task. Always use within a lock_guard as
         * it might change when attempting to submit a new task.
         *
         * @return Returns true if the thread pool is not full, false otherwise.
         */
        bool is_thread_pool_full() const {
            const unsigned int v_thread_count = m_thread_pool.get_thread_count();
            const unsigned int v_tasks_running = m_thread_pool.get_tasks_running();
            return v_tasks_running == v_thread_count;
        }


        void prune_and_fix_parent_root_if_needed(node &p_node) {
            node v_parent = p_node.get_parent();
            const node v_root = p_node.get_root();
            if (p_node == v_root && p_node.get_children_count() == 0) {
                return; // p_node was root and a leaf — nothing to fix
            }
            p_node.prune();

            /**
             * Scenarios to consider (binary): (■ = pruned node, x = consumed child, o = pending child)
             *               root
             *               /  \
             *              ■    o ← This will become the new root
             *
             *               root
             *               /  \
             *              x    o
             *               \
             *                x
             *                 \
             *                  x   ← This will become the new root
             *                 / \
             *                ■   o
             *
             * Root correction is done only when the root ends up with a single child after pruning.
             **/

            try_correct_root(v_parent);
        }

        bool send(node &p_node) {
            while (true) {
                const std::unique_lock v_lock(m_recursive_mutex, std::try_to_lock);
                if (!v_lock.owns_lock()) {
                    return false;
                }
                if (is_thread_pool_full()) {
                    return false;
                }
                const bool v_node_found_and_pushed = try_push_root_level_node_locally(p_node);
                if (v_node_found_and_pushed) {
                    // if top holder found, then it was pushed
                    continue; // keeps iterating from root to current level
                }
                // push current node
                if (p_node.is_consumed()) {
                    utils::log_and_throw("Node: {} is already consumed", p_node.get_node_id());
                }

                // after this line, only leftMost node should be pushed

                // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
                prune_and_fix_parent_root_if_needed(p_node);
                if (p_node.should_branch()) {
                    p_node.delegate_locally(this);
                } else {
                    p_node.set_state(DISCARDED);
                }
                return true; // signalize that node was consumed
            }
        }

        bool send(node &p_node, const int p_runnable_id) {
            while (true) {
                const std::unique_lock v_lock(m_recursive_mutex, std::try_to_lock);
                if (!v_lock.owns_lock()) {
                    return false;
                }

                const std::optional<transmission_guard> v_lock_optional = m_scheduler_worker->try_open_transmission_channel();
                if (!v_lock_optional.has_value()) {
                    return false;
                }
                const bool v_node_found_and_pushed = try_push_root_level_node_remotely(p_node, p_runnable_id);
                if (v_node_found_and_pushed) {
                    // if top holder found, then it was pushed
                    continue; // keeps iterating from root to current level
                }
                // push current node
                if (p_node.is_consumed()) {
                    utils::log_and_throw("Node: {} is already consumed", p_node.get_node_id());
                }

                // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
                prune_and_fix_parent_root_if_needed(p_node);
                if (p_node.should_branch()) {
                    p_node.delegate_remotely(m_scheduler_worker, p_runnable_id);
                } else {
                    p_node.set_state(DISCARDED);
                }
                return true; // signalize that node was consumed
            }
        }

        bool try_push_root_level_node_locally(node &p_node) {

            node v_top_node = p_node.map_core([&](const std::shared_ptr<node_core> &p_core) { return get_root_level_pending_node(p_core); });
            if (v_top_node != nullptr) {
                if (v_top_node.is_consumed()) {
                    utils::log_and_throw("Attempt to push a consumed node");
                }

                try_correct_root(p_node); // VERY IMPORTANT!!
                if (v_top_node.should_branch()) {
                    v_top_node.delegate_locally(this);
                } else {
                    v_top_node.set_state(DISCARDED);
                }
                // root-level node found whether discarded or pushed
                return true;
            }
            return false;
        }

        bool try_push_root_level_node_remotely(node &p_node, const int p_runnable_id) {

            node v_top_node = p_node.map_core([&](const std::shared_ptr<node_core> &p_core) { return get_root_level_pending_node(p_core); });
            if (v_top_node != nullptr) {
                if (v_top_node.is_consumed()) {
                    utils::log_and_throw("Attempt to push a consumed node");
                }

                try_correct_root(p_node); // VERY IMPORTANT!!
                if (v_top_node.should_branch()) {
                    v_top_node.delegate_remotely(m_scheduler_worker, p_runnable_id);
                } else {
                    v_top_node.set_state(DISCARDED);
                }
                // root-level node found whether discarded or pushed
                return true;
            }
            return false;
        }

        /**
         * It finds a pending node at the root level, prunes it from the root, and returns it.
         *
         * @param p_node The node from which to start the search.
         * @return The pending node at the root level, or nullptr if none found.
         */
        static std::shared_ptr<node_core> get_root_level_pending_node(const std::shared_ptr<node_core> &p_node) {
            if (p_node->get_parent() == nullptr) {
                return nullptr; // there is no parent — node is a root
            }
            if (p_node->get_parent() == p_node->get_root()) {
                // it avoids pushing the right most siblings first at the root level
                return nullptr; // parent == root
            }

            const std::shared_ptr<node_core> v_root = p_node->get_root();

            if (!v_root) {
                return nullptr;
            }

            // Defensive check
            if (v_root->get_children_count() < 2) {
                // Not really sure how we can get here, but just in case
                return nullptr;
            }

            if (v_root->get_children_count() > 2) {
                // Multiple pending nodes: at least three possible branches
                // 1. leftmost (already pushed)
                // 2. second leftmost (pending) <-- to be returned
                // 3. others (waiting)

                std::shared_ptr<node_core> v_second_child = v_root->get_second_leftmost_child();
                v_second_child->prune();
                return v_second_child;
            }

            // This conditions should be met:  v_root->get_children_count() == 2

            if (v_root->get_children_count() != 2) {
                utils::log_and_throw("Unexpected condition: root should have exactly two children");
            }

            // Last pending node - return it
            std::shared_ptr<node_core> v_second = v_root->get_second_leftmost_child();

            if (v_second == nullptr) {
                utils::log_and_throw("Unexpected condition: second child is null");
            }

            v_second->prune();
            // at this point, root should have only one child: the leftMost which was already resolved
            return v_second; // Just return it
        }

        /**
         * <pre>this is useful because at level zero of a root, there might be multiple
         * waiting nodes, though the <code>leftMost</code> branch (at zero level) might be at one of
         * the very right subbranches deep down, which means that there is a line of
         * multiple nodes with a single child.
         * </pre>
         * <pre>
         * A node_ with a single child means that it has already been solved and
         * also its siblings, because children are unlinked from their parent node_
         * when these are pushed or fully solved (returned)
         * </pre>
         *
         * @code
         *                    root
         *                    /|\
         *                  /  | \
         *                /    |  \
         *               x     o   o
         *                \
         *                 x
         *                  \
         *                   x
         *                    \
         *                     ■
         * @endcode
         *
         * <pre>
         * if there are available threads, and all waiting nodes at level zero are pushed,
         * then the root should be lowered down where it finds a node_ with at least two children or
         * the deepest node_
         * </pre>
         **/
        void try_correct_root(node &p_node) {
            node v_root = p_node.get_root();
            // Check if root is null (shouldn't happen with new prune(), but defensive)
            if (v_root == nullptr) {
                return;
            }

            // Check if p_node is already its own root (was pruned)
            if (v_root == p_node && v_root.get_children_count() == 0) {
                return; // Nothing to correct - node is already root and a leaf
            }

            if (v_root.get_children_count() >= 2) {
                return; // root has multiple children, no need to correct
            }

            // Root has 0 or 1 children, need to lower it
            while (v_root.get_children_count() == 1) {
                node v_leftmost = v_root.get_leftmost_child();
                make_root(v_leftmost);
                v_root.prune();
                v_root = v_leftmost;

                // Check if we've reached p_node or a node with 2+ children
                if (v_root == p_node || v_root.get_children_count() >= 2) {
                    return;
                }
            }
            // After exiting loop, v_root either:
            // - has 0 children (is a leaf) - it's already the root
            // - has 2+ children (has pending work) - it's already the root
        }

        void make_root(node &p_node) {
            p_node.map_core([this](std::shared_ptr<node_core> &p_core) {
                this->set_root(p_core->get_thread_id(), p_core);
                p_core->set_parent(nullptr);
                return p_core;
            });
        }
    };


} // namespace gempba


#endif // GEMPBA_QUASI_HORIZONTAL_LOAD_BALANCER_HPP
