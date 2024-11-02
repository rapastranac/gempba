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

#ifndef GEMPBA_QUASIHORIZONTALLOADBALANCER_HPP
#define GEMPBA_QUASIHORIZONTALLOADBALANCER_HPP

#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include "load_balancing/api/load_balancer.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-31
 */
namespace gempba {

    class QuasiHorizontalLoadBalancer final : public LoadBalancer {
    private:
        std::map<int, void *> roots; // every thread will be solving a subtree, this point to their roots
        std::mutex mtx;
        std::atomic<long long> idleTime{0};
        int idCounter = 0;

    protected:
        QuasiHorizontalLoadBalancer() = default;

    public:

        static QuasiHorizontalLoadBalancer *getInstance();

        ~QuasiHorizontalLoadBalancer() override = default;

        int getUniqueId() override;

        [[nodiscard]] long long int getIdleTime() const override;

        void accumulateIdleTime(long nanoseconds) override;

        void setRoot(int threadId, TraceNode *root) override;

        TraceNode **getRoot(int threadId) override;

        TraceNode *findTopTraceNode(TraceNode &node) override;

        /**
         * @brief controls the root for sequential calls
         *
         * Having the following tree
         * @code
         *                 root == parent
         *                 /  |  \   \  \
         *                /   |   \   \   \
         *               /    |    \   \    \
         *             p<sub>b</sub>     c<sub>b</sub>    w<sub>1</sub>  w<sub>2</sub> ... w<sub>k</sub>
         *             △ -->
         * @endcode
         * <ul>
         *   <li> p<sub>b</sub>	stands for previous branch </li>
         *   <li> c<sub>b</sub>	stands for current branch </li>
         *   <li> w<sub>i</sub>	stands for waiting branch, or target node i={1...k} </li>
         * </ul>
         *
         * <pre>
         * if p<sub>b</sub> is fully solved sequentially or w<sub>i</sub> were pushed but there is at least
         * one <code>w<sub>i</sub></code> remaining, then the thread will return to first level where the
         * parent is also the root, then the <code>leftMost</code> child of the root should be
         * deleted of the list since it is already solved. Thus, pushing c<sub>b</sub> twice
         * is avoided because <code>findTopTraceNode()</code> pushes the second element of the children
         * </pre>
         */
        void maybePruneLeftSibling(TraceNode &node) override;

        /**
         * @brief controls the root when successful parallel calls (if no <code>upperNode</code> available)
         *
         * this method is invoked when the <code>LoadBalancer</code> is enabled and the method <code>findTopTraceNode()</code> was not able to find
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
        void pruneLeftSibling(TraceNode &node) override;

        void prune(TraceNode &node) override;

        void reset() override;

    private:

        void lowerRoot(TraceNode &node);

        /**
         * <pre>this is useful because at level zero of a root, there might be multiple
         * waiting nodes, though the <code>leftMost</code> branch (at zero level) might be at one of
         * the very right subbranches deep down, which means that there is a line of
         * multiple nodes with a single child.
         * </pre>
         * <pre>
         * A node with a single child means that it has already been solved and
         * also its siblings, because children are unlinked from their parent node
         * when these ones are pushed or fully solved (returned)
         * </pre>
         * 
         * @code
         *                    root == parent
         *                  /  |  \   \  \
         *                 /   |   \   \   \
         *                /    |    \   \    \
         *        leftMost     w<sub>1</sub>    w<sub>2</sub>  w<sub>3</sub> ... w<sub>k</sub>
         *                \
         *                 *
         *                  \
         *                   *
         *                    \
         *                current_level
         * @endcode
         * <pre>
         * if there are available threads, and all waiting nodes at level zero are pushed,
         * then the root should be lowered down where it finds a node with at least two children or
         * the deepest node
         * </pre>
         */
        void maybe_correct_root(TraceNode *node);
    };

}

#endif //GEMPBA_QUASIHORIZONTALLOADBALANCER_HPP
