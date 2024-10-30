#include "load_balancing/impl/quasi_horizontal_load_balancer.hpp"

namespace gempba {
    QuasiHorizontalLoadBalancer *QuasiHorizontalLoadBalancer::getInstance() {
        static auto *instance = new QuasiHorizontalLoadBalancer();
        return instance;
    }

    int gempba::QuasiHorizontalLoadBalancer::getUniqueId() {
        std::scoped_lock<std::mutex> lck(mtx);
        return ++idCounter;
    }

    long long int QuasiHorizontalLoadBalancer::getIdleTime() const {
        return idleTime.load(std::memory_order_relaxed);
    }

    void QuasiHorizontalLoadBalancer::accumulateIdleTime(long nanoseconds) {
        this->idleTime.fetch_add(nanoseconds, std::memory_order_relaxed);
    }

    void QuasiHorizontalLoadBalancer::setRoot(int threadId, TraceNode *root) {
        std::scoped_lock<std::mutex> lck(mtx);
        roots[threadId] = root;
    }

    TraceNode **QuasiHorizontalLoadBalancer::getRoot(int threadId) {
        return roots.contains(threadId) ? reinterpret_cast<TraceNode **>(&roots[threadId]) : nullptr;
    }

    TraceNode *QuasiHorizontalLoadBalancer::findTopTraceNode(TraceNode &node) {
        TraceNode *leftMost; // this is the branch that led us to the root
        TraceNode *root;     // local pointer to root, to avoid "*" use

        if (node.getParent() == nullptr) {
            return nullptr; // there is no parent
        }
        // Hereto, there might be a root
        if (node.getParent() == *node.getRoot()) {
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
        root = *node.getRoot(); // no need to iterate
        // int tmp = root->getChildrenCount(); // this probably fix the following

        // the following is not true, it could be also the right branch
        // Unless root is guaranteed to have at least 2 children,
        // TODO ... verify

        leftMost = root->getFirstChild(); // TODO ... check if the branch has been pushed or forwarded

        utils::print_mpi_debug_comments("rank {}, likely to get an upperNode \n", -1);
        utils::print_mpi_debug_comments("rank {}, root->getChildrenCount() = {} \n", -1, root->getChildrenCount());

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
        if (root->getChildrenCount() > 2) {
            /**
             * this condition is for multiple recursion (>2), the difference with the one below is that
             * the root does not move after returning one of the waiting nodes,
             * say we have the following root's children
             * @code
             *   children =	{c<sub>b</sub>,w<sub>1</sub>,w<sub>2</sub> ... w<sub>k</sub>}
             * @endcode
             *   the goal is to push <code>w<sub>1</sub></code>, which is the immediate right node
             */
            TraceNode *second_child = root->getSecondChild();
            root->pruneSecondChild();
            return second_child;
        } else if (root->getChildrenCount() == 2) {
            utils::print_mpi_debug_comments("rank {}, about to choose an upperNode \n", -1);
            /**
             * <pre>
             * this scope is meant to push the right branch which was put in the waiting line
             * because there was no available thread to push the <code>leftMost</code> branch, then <code>leftMost</code>
             * will be the new root because after this scope the right branch will have been already pushed
             * </pre>
             */

            root->pruneFrontChild();                    // deletes leftMost from root's children
            TraceNode *right = root->getFirstChild();   // The one to be pushed
            root->pruneFrontChild();                    // there should not be anything left in the children list

            this->prune(*right);            // just in case, the right branch is not being sent anyway, only its data is
            this->lowerRoot(*leftMost);     // it sets leftMost as the new root

            maybe_correct_root(leftMost);
            /**
             * if <code>leftMost</code> has no pending branch, then root will be assigned to the next
             * descendant with at least two children (which is at least a pending branch),
             * or the lowest branch which is the one giving priority to root's children
             * */

            return right;
        }

        /**
         * this should not happen because when the root get only two children, the root is lowered to either the last node
         * down the line, or the firs node from top-to-bottom with at least two children
         */


        spdlog::error("fw_count : {} \n ph_count : {}\n isVirtual :{} \n isDiscarded : {} \n",
                      root->getForwardCount(),
                      root->getPushCount(),
                      root->isDummy(),
                      root->getState() == DISCARDED);
        spdlog::throw_spdlog_ex("4 Testing, it's not supposed to happen, <code>findTopTraceNode()</code>");
    }

    void QuasiHorizontalLoadBalancer::maybePruneLeftSibling(TraceNode &node) {
        TraceNode *parent = node.getParent();
        if (parent == nullptr) {
            // This node is a root, nothing to prune
            return;
        }

        TraceNode *_leftMost = parent->getFirstChild();
        if (_leftMost == &node) {
            // node is the leftMost child, no need to prune nor correct the root
            return;
        }

        if (parent == *node.getRoot()) {
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
            std::set<gempba::TraceNode *> leftSiblings;
            while (_leftMost != &node) {
                leftSiblings.insert(_leftMost);
                parent->pruneFrontChild();        // removes pb from the parent's children
                _leftMost = parent->getFirstChild(); // it gets what it was the second element from the parent's children
            }
            // after this line,this should be true leftMost == node

            // There might be more than one remaining sibling
            if (parent->getChildrenCount() > 1) {
                for (const auto &toBePruned: leftSiblings) {
                    this->prune(*toBePruned);
                }
                return; // root does not change
            }

            /**
             * if the node is the only remaining child from the parent
             * then this node will become a new root
             * */
            parent->pruneFrontChild(); // removes remaining node from the parent's children

            this->lowerRoot(node);
            this->prune(*parent);
            for (const auto &toBePruned: leftSiblings) {
                this->prune(*toBePruned);
            }
            maybe_correct_root(&node);
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
        parent->pruneFrontChild();
    }

    void QuasiHorizontalLoadBalancer::pruneLeftSibling(TraceNode &node) {
        TraceNode *_parent = node.getParent();
        if (_parent == nullptr) {
            return;
        }
        // it also confirms that node is not a parent (applies for LoadBalancer)
        if (_parent->getChildrenCount() > 2) {
            TraceNode *front = _parent->getFirstChild();
            _parent->pruneFrontChild();
            prune(*front);
            return;
        }

        if (_parent->getChildrenCount() == 2) {
            // this verifies that it is binary and the rightMost will become a new root
            TraceNode *front = _parent->getFirstChild();
            _parent->pruneFrontChild();
            this->prune(*front);
            TraceNode *rightMost = _parent->getFirstChild();
            _parent->pruneFrontChild();
            this->lowerRoot(*rightMost);
            this->prune(*_parent);
            return;
        }

        spdlog::throw_spdlog_ex("4 Testing, it's not supposed to happen, pruneLeftSibling()\n");
    }

    void QuasiHorizontalLoadBalancer::prune(TraceNode &node) {
        node.setRoot(nullptr);
        node.setParent(nullptr);
    }

    void QuasiHorizontalLoadBalancer::reset() {
        roots.clear();
        idleTime.store(0, std::memory_order::relaxed);
        idCounter = 0;
    }

    void QuasiHorizontalLoadBalancer::lowerRoot(TraceNode &node) {
        setRoot(node.getThreadId(), &node);
        node.setParent(nullptr);
    }

    void QuasiHorizontalLoadBalancer::maybe_correct_root(TraceNode *node) {
        TraceNode *_root = node;
        TraceNode *_parent = node;

        while (_root->getChildrenCount() == 1) { // lowering the root
            _root = _root->getFirstChild();
            _parent->pruneFrontChild();
            this->lowerRoot(*_root);
            this->prune(*_parent);
            _parent = _root;
        }
    }
}