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

#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/utils.hpp>
#include <gempba/core/load_balancer.hpp>
#include <gempba/core/node.hpp>

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
        std::map<std::thread::id, std::shared_ptr<std::shared_ptr<node_core> > > m_roots; // every thread will be solving a subtree, this point to their roots
        unsigned int m_thread_count = std::thread::hardware_concurrency();
        std::size_t m_thread_request_count = 0;

    public:
        explicit quasi_horizontal_load_balancer(scheduler::worker *const p_scheduler_worker = nullptr) :
            m_scheduler_worker(p_scheduler_worker) {
        }

        balancing_policy get_balancing_policy() override {
            return QUASI_HORIZONTAL;
        }

        unsigned generate_unique_id() override {
            return ++m_unique_id_counter;
        }

        std::future<std::any> force_local_submit(std::function<std::any()> &&p_function) override {
            ++m_thread_request_count;
            return m_thread_pool.submit_task(p_function);
        }

        void forward(node &p_node) override {
            if (p_node.get_state() == UNUSED && p_node.should_branch()) {
                p_node.run();
            }
            node v_parent = p_node.get_parent();
            const node v_root = p_node.get_root();
            p_node.prune();
            if (v_parent != nullptr && v_parent == v_root) {
                v_parent.apply_core([this](const std::shared_ptr<node_core> &p_core) {
                    maybe_correct_root(p_core);
                });
            }
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
                spdlog::throw_spdlog_ex("Attempted to do remote submission without a scheduler worker");
            }
            if (send(p_node, p_runnable_id)) {
                return true;
            }
            try_local_submit(p_node);
            return false;
        }

        [[nodiscard]] double get_idle_time() const override {
            return m_thread_pool.get_wall_idle_time();
        }

        void set_root(const std::thread::id p_thread_id, std::shared_ptr<node_core> &p_root) override {
            std::scoped_lock v_lock(m_recursive_mutex);
            if (m_roots.contains(p_thread_id)) {
                const auto v_ptr = m_roots.at(p_thread_id);
                *v_ptr = p_root; // should I assign this way
            } else {
                m_roots.emplace(p_thread_id, std::make_shared<std::shared_ptr<node_core> >(p_root));
            }
        }

        std::shared_ptr<std::shared_ptr<node_core> > get_root(const std::thread::id p_thread_id) override {
            if (m_roots.contains(p_thread_id)) {
                return m_roots.at(p_thread_id);
            }
            return nullptr;
        }

        void set_thread_pool_size(const unsigned p_size) override {
            m_thread_pool.reset(p_size);
            spdlog::debug("thread pool size: {}", m_thread_pool.get_thread_count());
        }

        void wait() override {
            m_thread_pool.wait();
        }

        bool is_done() const override {
            return m_thread_pool.get_tasks_total() == 0;
        }

        std::size_t get_thread_request_count() const override {
            return m_thread_request_count;
        }

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
                    spdlog::throw_spdlog_ex("Node: {} is already consumed", p_node.get_node_id());
                }

                // after this line, only leftMost node should be pushed

                // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
                p_node.prune();
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
                    spdlog::throw_spdlog_ex("Node: {} is already consumed", p_node.get_node_id());
                }

                // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
                p_node.prune();
                if (p_node.should_branch()) {
                    p_node.delegate_remotely(m_scheduler_worker, p_runnable_id);
                } else {
                    p_node.set_state(DISCARDED);
                }
                return true; // signalize that node was consumed
            }
        }

        bool try_push_root_level_node_locally(node &p_node) {

            node v_top_node = p_node.map_core([&](const std::shared_ptr<node_core> &p_core) { return this->find_top_node(p_core); });
            if (v_top_node != nullptr) {
                if (v_top_node.is_consumed()) {
                    spdlog::throw_spdlog_ex("Attempt to push a consumed node");
                }

                v_top_node.prune(); // VERY IMPORTANT!! 
                if (v_top_node.should_branch()) {
                    v_top_node.delegate_locally(this);
                } else {
                    v_top_node.set_state(DISCARDED);
                }
                // root-level node found whether discarded or pushed
                return true;
            }
            // TODO ... double check this, why is not top_node
            //  I see, because find_top_node also corrects the root. Rename method!
            p_node.apply_core([&](const std::shared_ptr<node_core> &p_core) { this->prune_left_sibling(p_core); });
            return false;
        }

        bool try_push_root_level_node_remotely(node &p_node, const int p_runnable_id) {

            node v_top_node = p_node.map_core([&](std::shared_ptr<node_core> p_core) { return this->find_top_node(p_core); });
            if (v_top_node != nullptr) {
                if (v_top_node.is_consumed()) {
                    spdlog::throw_spdlog_ex("Attempt to push a consumed node");
                }

                v_top_node.prune(); // VERY IMPORTANT!!
                if (v_top_node.should_branch()) {
                    v_top_node.delegate_remotely(m_scheduler_worker, p_runnable_id);
                } else {
                    v_top_node.set_state(DISCARDED);
                }
                // root-level node found whether discarded or pushed
                return true;
            }
            // TODO ... double check this, why is not top_node
            //  I see, because find_top_node also corrects the root. Rename method!
            p_node.apply_core([&](const std::shared_ptr<node_core> &p_core) { this->prune_left_sibling(p_core); });
            return false;

        }

        std::shared_ptr<node_core> find_top_node(const std::shared_ptr<node_core> &p_node) {
            std::shared_ptr<node_core> v_leftmost; // this is the branch that led us to the root
            std::shared_ptr<node_core> v_root; // local pointer to root, to avoid "*" use

            if (p_node->get_parent() == nullptr) {
                return nullptr; // there is no parent
            }
            // Hereto, there might be a root
            if (p_node->get_parent() == p_node->get_root()) {
                return nullptr; // parent == root
            }
            /**
             * <pre>Hereto:
             * <ul>
             *  <li>the root isn't the parent</li>
             *  <li>the branch has already been pushed, to ensure pushing the <code>leftMost</code> first </li>
             * </ul>
             * </pre>
             */
            v_root = p_node->get_root(); // no need to iterate
            // int tmp = root->get_children_count(); // this probably fix the following

            // the following is not true, it could be also the right branch
            // Unless root is guaranteed to have at least 2 children,
            // TODO ... verify

            v_leftmost = v_root->get_leftmost_child(); // TODO ... check if the branch has been pushed or forwarded

            utils::print_ipc_debug_comments("rank {}, likely to get an upperNode \n", -1);
            utils::print_ipc_debug_comments("rank {}, root->get_children_count() = {} \n", -1, v_root->get_children_count());

            /**
             * Here below, we check is the left child was pushed to the thread pool, then the pointer to its parent is pruned
             * @code
             *                    parent
             *                 /  |  \   \  \
             *                /   |   \   \   \
             *               /    |    \   \    \
             *             p<sub>b</sub>     c<sub>b</sub>    w<sub>1</sub>  w<sub>2</sub> ... w<sub>k</sub>
             *             △ -->
             * @endcode
             * <ul>
             *   <li> p<sub>b</sub>	stands for pushed branch </li>
             *   <li> c<sub>b</sub>	stands for current branch </li>
             *   <li> w<sub>i</sub>	stands for waiting branch, or target node i={1...k} </li>
             * </ul>
             *
             * if <code>p<sub>b</sub></code> is already pushed, it won't be part of the children list of <code>parent</code>,
             * then <code>list = {c<sub>b</sub>,w<sub>1</sub>,w<sub>2</sub>}</code>
             * @code
             *   leftMost = c<sub>b</sub>
             *   nextElt = w<sub>1</sub>
             * @endcode
             *
             * <pre>
             * There will never be fewer than two elements, assuming multiple recursions per scope,
             * because as long as there remain two elements, it implies that the rightMost element
             * will be pushed to the pool, and then the leftMost element will no longer need a parent.
             * This condition is the first one to explore at this level of the tree.
             * </pre>
             */
            if (v_root->get_children_count() > 2) {
                /**
                 * this condition is for multiple recursion (>2), the difference with the one below is that
                 * the root does not move after returning one of the waiting nodes,
                 * say we have the following root's children
                 * @code
                 *   children =	{c<sub>b</sub>,w<sub>1</sub>,w<sub>2</sub> ... w<sub>k</sub>}
                 * @endcode
                 *   the goal is to push <code>w<sub>1</sub></code>, which is the immediate right node
                 */
                std::shared_ptr<node_core> v_second_child = v_root->get_second_leftmost_child();
                v_root->remove_second_leftmost_child();
                return v_second_child;
            } else if (v_root->get_children_count() == 2) {
                utils::print_ipc_debug_comments("rank {}, about to choose an upperNode \n", -1);
                /**
                 * <pre>
                 * this scope is meant to push the right branch which was put in the waiting line
                 * because there was no available thread to push the <code>leftMost</code> branch, then <code>leftMost</code>
                 * will be the new root because after this scope the right branch will have been already pushed
                 * </pre>
                 */

                v_root->remove_leftmost_child(); // deletes leftMost from root's children
                std::shared_ptr<node_core> v_right = v_root->get_leftmost_child(); // The one to be pushed
                v_root->remove_leftmost_child(); // there should not be anything left in the children list

                this->prune(v_right); // just in case, the right branch is not being sent anyway, only its data is
                this->lower_root(v_leftmost); // it sets leftMost as the new root

                maybe_correct_root(v_leftmost);
                /**
                 * if <code>leftMost</code> has no pending branch, then root will be assigned to the next
                 * descendant with at least two children (which is at least a pending branch),
                 * or the lowest branch which is the one giving priority to root's children
                 * */

                return v_right;
            }

            /**
             * this should not happen because when the root get only two children, the root is lowered to either the last node
             * down the line, or the firs node from top-to-bottom with at least two children
             */


            spdlog::error("fw_count : {} \n ph_count : {}\n isVirtual :{} \n isDiscarded : {} \n", v_root->get_forward_count(),
                          v_root->get_push_count(), v_root->is_dummy(), v_root->get_state() == DISCARDED);
            spdlog::throw_spdlog_ex("4 Testing, it's not supposed to happen, <code>findTopNode()</code>");
        }

        /**
         * @brief controls the root for sequential calls
         *
         * Having the following tree
         * @code
         *                  root == parent
         *                 /  |  \   \  \
         *                /   |   \   \   \
         *               /    |    \   \    \
         *             p<sub>b</sub>     c<sub>b</sub>    w<sub>1</sub>  w<sub>2</sub> ... w<sub>k</sub>
         *             △ -->
         * @endcode
         * <ul>
         *   <li> p<sub>b</sub>	stands for previous branch </li>
         *   <li> c<sub>b</sub>	stands for current branch </li>
         *   <li> w<sub>i</sub>	stands for waiting branch, or target node_ i={1...k} </li>
         * </ul>
         *
         * <pre>
         * if p<sub>b</sub> is fully solved sequentially or w<sub>i</sub> were pushed but there is at least
         * one <code>w<sub>i</sub></code> remaining, then the thread will return to first level where the
         * parent is also the root, then the <code>leftMost</code> child of the root should be
         * deleted of the list since it is already solved. Thus, pushing c<sub>b</sub> twice
         * is avoided because <code>findTopNode()</code> pushes the second element of the children
         * </pre>
         */
        void maybe_prune_left_sibling(std::shared_ptr<node_core> &p_node) {
            std::shared_ptr<node_core> v_parent = p_node->get_parent();
            if (v_parent == nullptr) {
                // This node is a root, nothing to prune
                return;
            }

            std::shared_ptr<node_core> v_leftmost = v_parent->get_leftmost_child();
            if (v_leftmost == p_node) {
                // node is the leftMost child, no need to prune nor correct the root
                return;
            }

            if (v_parent == p_node->get_root()) {
                /**
                 * @brief this confirms that it's the first level of the root
                 * @code
                 *    root == parent
                 *     /  |  \   \   \
                 *    /   |   \   \    \
                 *   /    |    \   \     \
                 * p<sub>b</sub>     c<sub>b</sub>    w<sub>1</sub>  w<sub>2</sub> ... w<sub>k</sub>
                 *        **
                 * @endcode
                 * <pre>
                 * next <code>if-statement</code> should always evaluate to true, it should not be necessary
                 * to use a loop.Therefore, this <code>while</code> should ideally run only once.
                 * This is important for testing purposes.
                 * </pre>
                 */
                std::set<std::shared_ptr<node_core> > v_left_siblings;
                while (v_leftmost != p_node) {
                    v_left_siblings.insert(v_leftmost);
                    v_parent->remove_leftmost_child(); // removes pb from the parent's children
                    v_leftmost = v_parent->get_leftmost_child(); // it gets what it was the second element from the parent's children
                }
                // after this line,this should be true leftMost == node

                // There might be more than one remaining sibling
                if (v_parent->get_children_count() > 1) {
                    for (const auto &v_to_be_pruned: v_left_siblings) {
                        this->prune(v_to_be_pruned);
                    }
                    return; // root does not change
                }

                /**
                 * if the node is the only remaining child from the parent
                 * then this node will become a new root
                 * */
                v_parent->remove_leftmost_child(); // removes remaining node from the parent's children

                this->lower_root(p_node);
                this->prune(v_parent);
                for (const auto &v_to_be_pruned: v_left_siblings) {
                    this->prune(v_to_be_pruned);
                }
                maybe_correct_root(p_node);
                return;
            }
            // Any other level

            /**
             * @code
             *         root != parent
             *           /|  \   \  \
             *          / |   \   \   \
             *    solved  *    w<sub>1</sub>  w<sub>2</sub> .. W<sub>k</sub>
             *           /|
             *    solved  *
             *           /|
             *    solved  * parent
             *           / \
             * solved(p<sub>b</sub>)  c<sub>b</sub>
             * @endcode
             *
             * <pre>
             *  This is relevant because although the root still has some waiting nodes,
             *  the thread in charge of the tree might be deep down solving everything sequentially.
             *  Every time a <code>leftMost</code> branch is solved sequentially, this one should be removed from
             *  the list to avoid failure attempts of solving a branch that has already been consumed.
             * </pre>
             * <pre>
             *  If a thread attempts to solve a consumed branch, this will throw an error
             *  because the node won't have information anymore since it has already been passed on
             * </pre>
             *
             *
             * By here, <code>p<sub>b</sub></code> has already been solved
             * <pre>
             *  This scope only deletes the <code>leftMost</code> node, which is already
             * solved sequentially by here and leaves the parent with at
             * least a child because the root still has at least a node in
             * the waiting list
             * </pre>
             */
            v_parent->remove_leftmost_child();
        }


        /**
         * @brief controls the root when successful parallel calls (if no <code>upperNode</code> available)
         *
         * this method is invoked when the <code>load_balancer</code> is enabled and the method <code>findTopNode()</code> was not able to find
         *  a top branch to push, because it means the next right sibling will become a root(for binary recursion)
         *  or just the <code>leftMost</code> will be unlisted from the parent's children. This method is invoked if and only if
         *  a thread is available
         *
         *      In this scenario, the root does not change
         * @code
         *                  parent == root          (potentially dummy)
         *                       /    \    \
         *                      /      \      \
         *                     /        \        \
         *                  left        next     right
         *              (pushed or
         *              sequential)
         * @endcode
         *
         *      In the following scenario the remaining right child becomes the root, because the right child was pushed
         * @code
         *                  parent == root          (potentially dummy)
         *                       /    \
         *                      /      \
         *                     /        \
         *                  left        right
         *              (pushed or      (new root)
         *              sequential)
         * @endcode
         */
        void prune_left_sibling(const std::shared_ptr<node_core> &p_node) {
            const std::shared_ptr<node_core> v_parent = p_node->get_parent();
            if (v_parent == nullptr) {
                return;
            }
            // it also confirms that node is not a parent (applies for load_balancer)
            if (v_parent->get_children_count() > 2) {
                std::shared_ptr<node_core> v_leftmost = v_parent->get_leftmost_child();
                // v_parent->remove_leftmost_child();
                // v_parent->remove_child(v_leftmost);
                prune(v_leftmost);
                return;
            }

            if (v_parent->get_children_count() == 2) {
                // this verifies that it is binary and the rightMost will become a new root
                std::shared_ptr<node_core> v_leftmost = v_parent->get_leftmost_child();
                // v_parent->remove_leftmost_child();
                // v_parent->remove_child(v_leftmost);
                prune(v_leftmost);
                std::shared_ptr<node_core> v_rightmost = v_parent->get_leftmost_child();
                // v_parent->remove_leftmost_child();
                v_parent->remove_child(v_rightmost);
                this->lower_root(v_rightmost);
                this->prune(v_parent);
                return;
            }

            spdlog::throw_spdlog_ex("4 Testing, it's not supposed to happen, pruneLeftSibling()\n");
        }

        void prune(const std::shared_ptr<node_core> &p_node) {
            p_node->prune();
            // p_node->set_root(nullptr);
            // p_node->set_parent(nullptr);
        }

        void lower_root(std::shared_ptr<node_core> &p_node) {
            set_root(p_node->get_thread_id(), p_node);
            p_node->set_parent(nullptr);
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
         *                   root == parent
         *                  /  |  \   \  \
         *                 /   |   \   \   \
         *                /    |    \   \    \
         *        leftMost     w<sub>1</sub>    w<sub>2</sub>  w<sub>3</sub> ... w<sub>k</sub>
         *                \
         *                 x
         *                  \
         *                   x
         *                    \
         *                current_level
         * @endcode
         *
         * <pre>
         * if there are available threads, and all waiting nodes at level zero are pushed,
         * then the root should be lowered down where it finds a node_ with at least two children or
         * the deepest node_
         * </pre>
         */
        void maybe_correct_root(const std::shared_ptr<node_core> &p_node) {
            std::shared_ptr<node_core> v_root = p_node;
            std::shared_ptr<node_core> v_parent = p_node;

            while (v_root->get_children_count() == 1) {
                // lowering the root
                v_root = v_root->get_leftmost_child();
                v_parent->remove_leftmost_child();
                this->lower_root(v_root);
                this->prune(v_parent);
                v_parent = v_root;
            }
        }

    };


}


#endif //GEMPBA_QUASI_HORIZONTAL_LOAD_BALANCER_HPP
