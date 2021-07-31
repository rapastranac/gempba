#ifndef DLB_HANDLER_HPP
#define DLB_HANDLER_HPP

#include <atomic>
#include <map>
#include <mutex>

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

/* 
- by default every holder is a root of itself and has no parent

- if a parent is passed at construction time, then a holder will have this as a parent
and it will adopt the parent's root as its root as well

- if two or more holders that don't have a parent get linked, at linking time, a dummy holder will
be constructed which will act as a root and it is considered virtual since its only purpose
is to be a root. This allows to track all levels of the exploration tree, and therefore
all holders are potentally pushable
*/

namespace GemPBA
{
    template <typename _Ret, typename... Args>
    class ResultHolder;
    // Dynamic Load Balancing
    class DLB_Handler
    {
        template <typename _Ret, typename... Args>
        friend class ResultHolder;

    private:
        std::map<int, void *> roots; // every thread will be solving a sub tree, this point to their roots
        std::mutex mtx;
        std::atomic<long long> idleTime{0};

#ifdef R_SEARCH
        bool rootSearch = R_SEARCH;
#else
        bool rootSearch = false;
#endif
        size_t idCounter = 0;

        DLB_Handler() {}

    public:
        static DLB_Handler &getInstance()
        {
            static DLB_Handler instance;
            return instance;
        }

        size_t getUniqueId()
        {
            std::scoped_lock<std::mutex> lck(mtx);
            return ++idCounter;
        }

        void add_on_idle_time(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
        {
            double time_tmp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            idleTime.fetch_add(time_tmp, std::memory_order_relaxed);
        }

        // thread safe: root creation or root switching
        void assign_root(int threadId, void *root)
        {
            std::scoped_lock<std::mutex> lck(mtx);
            roots[threadId] = root;
        }

        template <typename Holder>
        Holder *find_top_holder(Holder *holder)
        {

            Holder *leftMost = nullptr; // this is the branch that led us to the root
            Holder *root = nullptr;     // local pointer to root, to avoid "*" use

            if (holder->parent) // this confirms there might be a root
            {
                if (holder->parent != *holder->root) // this confirms, the root isn't the parent
                {
                    /* this condition complies if a branch has already
					 been pushed, to ensure pushing leftMost first */
                    root = static_cast<Holder *>(*holder->root); //no need to iterate
                    //int tmp = root->children.size(); // this probable fix the following

                    // the following is not true, it could be also the right branch
                    // Unless root is guaranteed to have at least 2 children,
                    // TODO ... verify

                    leftMost = root->children.front(); //TODO ... check if branch has been pushed or forwarded
                }
                else
                    return nullptr; // parent == root
            }
            else
                return nullptr; // there is no parent

            //#ifdef DEBUG_COMMENTS
            //            fmt::print("rank {}, likely to get an upperHolder \n", -1);
            //#endif
            int N_children = root->children.size();

            //#ifdef DEBUG_COMMENTS
            //            fmt::print("rank {}, root->children.size() = {} \n", -1, N_children);
            //#endif

            /*Here below, we check is left child was pushed to pool, then the pointer to parent is pruned
							 parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  p     cb    w1  w2 ... wk

			p	stands for pushed branch
			cb	stands for current branch
			w	stands for waiting branch, or target holder
			Following previous diagram
			if "p" is already pushed, it won't be part of children list of "parent", then list = {cb,w1,w2}
			leftMost = cb
			nextElt = w1

			There will never be fewer than two elements, asuming multiple recursion per scope,
			because as long as it remains two elements, it means than rightMost element will be pushed to pool
			and then leftMost element will no longer need a parent, which is the first condition to explore
			this level of the tree*/

            if (root->children.size() > 2)
            {
                /* TO BE VERIFIED
                this condition is for multiple recursion, the diference with the one below is that
				the root does not move after returning one of the waiting nodes,
				say we have the following root's childen

				children =	{	cb	w1	w2	... wk}

				the goal is to push w1, which is the inmmediate right node */

                auto second = std::next(root->children.begin(), 1); // this is to access the 2nd element
                auto secondHolder = *second;                        // catches the pointer of the node	<-------------------------
                root->children.erase(second);                       // removes second node from the root's children

                return secondHolder;
            }
            else if (root->children.size() == 2)
            {
                //#ifdef DEBUG_COMMENTS
                //                fmt::print("rank {}, about to choose an upperHolder \n", -1);
                //#endif
                /*	this scope is meant to push right branch which was put in waiting line
					because there was no available thread to push leftMost branch, then leftMost
					will be the new root since after this scope right branch will have been
					already pushed*/

                root->children.pop_front();             // deletes leftMost from root's children
                Holder *right = root->children.front(); // The one to be pushed
                root->children.clear();                 // ..

                //right->prune();                         // just in case, right branch is not being sent anyway, only its data
                this->prune(right);

                this->lowerRoot(*leftMost);
                //leftMost->lowerRoot(); // it sets leftMost as the new root

                rootCorrecting(leftMost); // if leftMost has no pending branch, then root will be assigned to the next
                                          // descendant with at least two children (which is at least a pending branch),
                                          // or the lowest branch which is th one giving priority to root's children

                return right;
            }
            else
            {
                fmt::print("fw_count : {} \n ph_count : {}\n isVirtual :{} \n isDiscarded : {} \n",
                           root->fw_count,
                           root->ph_count,
                           root->isVirtual,
                           root->isDiscarded);
                throw std::runtime_error("4 Testing, it's not supposed to happen, find_top_holder()");
                return nullptr;
            }
        }

        // controls the root when sequential calls
        template <typename Holder>
        void checkLeftSibling(Holder *holder)
        {

            /* What does it do?. Having the following figure
						  root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  pb     cb    w1  w2 ... wk
					  △ -->
			pb	stands for previous branch
			cb	stands for current branch
			w_i	stands for waiting branch, or target holder i={1..k}

			if pb is fully solved sequentially or w_i were pushed but there is at least
				one w_i remaining, then thread will return to first level where the
				parent is also the root, then leftMost child of the root should be
				deleted of the list since it is already solved. Thus, pushing cb twice
				is avoided because find_top_holder() pushes the second element of the children
			*/

            if (holder->parent) //this confirms the holder is not a root
            {
                if (holder->parent == *holder->root) //this confirms that it's the first level of the root
                {
                    Holder *leftMost = holder->parent->children.front();
                    if (leftMost != holder) //This confirms pb has already been solved
                    {                       /*
						 root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
					  pb     cb    w1  w2 ... wk
					  		 **
					   next conditional should always comply, there should not be required
						* to use a loop, then this While is entitled to just a single loop. 4 testing!!
						*/
                        auto leftMost_cpy = leftMost;
                        while (leftMost != holder)
                        {
                            holder->parent->children.pop_front();        // removes pb from the parent's children
                            leftMost = holder->parent->children.front(); // it gets the second element from the parent's children
                        }
                        // after this line,this should be true leftMost == holder

                        // There might be more than one remaining sibling
                        if (holder->parent->children.size() > 1)
                            return; // root does not change

                        /* if holder is the only remaining child from parent then this means
						that it will have to become a new root*/

                        //holder->lowerRoot();
                        this->lowerRoot(*holder);

                        //leftMost_cpy->prune();
                        this->prune(leftMost_cpy);
                        //holder->parent = nullptr;
                        //holder->prune(); //not even required, nullptr is sent
                    }
                }
                else if (holder->parent != *holder->root) //any other level,
                {
                    /*
						 root != parent
						   /|  \   \  \
						  / |   \	 \	 \
					solved  *    w1  w2 . wk
					       /|
					solved	*
						   /|
					solved	* parent
						   / \
				 solved(pb)  cb


					this is relevant, because eventhough the root still has some waiting nodes
					the thread in charge of the tree might be deep down solving everything sequentially.
					Every time a leftMost branch is solved sequentially, this one should be removed from 
					the list to avoid failure attempts of solving a branch that has already been solved.

					If a thread attempts to solve an already solved branch, this will throw an error
					because the node won't have information anymore since it has already been passed
					*/

                    Holder *leftMost = holder->parent->children.front();
                    if (leftMost != holder) //This confirms pb has already been solved
                    {
                        /*this scope only deletes leftMost holder, which is already
						* solved sequentially by here and leaves the parent with at
						* least a child because the root still has at least a holder in
						* the waiting list
						*/
                        holder->parent->children.pop_front();
                    }
                }
            }
        }

        // controls the root when succesfull parallel calls ( if not upperHolder available)
        template <typename Holder>
        void pop_left_sibling(Holder *holder)
        {
            /* this method is invoked when DLB_Handler is enabled and the method find_top_holder() was not able to find
		a top branch to push, because it means the next right sibling will become a root(for binary recursion)
		or just the leftMost will be unlisted from the parent's children. This method is invoked if and only if 
		a thread is available

        In this scenario, the root does not change

                    parent == root          (potentially virtual)
                         /    \    \
                        /      \      \
                       /        \        \
                    left        next     right
                (pushed or
                sequential)


        In the following scenario the remaining right child becomes the root, because the right child was pushed
        
                    parent == root          (potentially virtual)
                         /    \
                        /      \
                       /        \
                    left        right
                (pushed or      (new root)
                sequential)

        */
            auto *_parent = holder->parent;
            if (_parent)                          // it should always comply, virtual parent is being created
            {                                     // it also confirms that holder is not a parent (applies for DLB_Handler)
                if (_parent->children.size() > 2) // this is for more than two recursions per scope
                {
                    _parent->children.pop_front();
                }
                else if (_parent->children.size() == 2) // this verifies that  it's binary and the rightMost will become a new root
                {
                    _parent->children.pop_front();
                    auto right = _parent->children.front();
                    _parent->children.pop_front();
                    //right->lowerRoot();
                    this->lowerRoot(*right);
                }
                else
                {
                    throw std::runtime_error("4 Testing, it's not supposed to happen, pop_left_sibling()\n");
                }
            }
        }

        template <typename Holder>
        void linkVirtualRoot_helper(Holder *parent, Holder &child)
        {
            child.parent = parent->itself;
            child.root = parent->root;
            parent->children.push_back(&child);
        }

        template <typename Holder, typename... Args>
        void linkVirtualRoot_helper(Holder *virtualRoot, Holder &child, Args &...args)
        {
            child.parent = virtualRoot->itself;
            child.root = virtualRoot->root;
            virtualRoot->children.push_back(&child);
        }

        template <typename Holder, typename... Args>
        void linkVirtualRoot(int threadId, Holder *virtualRoot, Holder &child, Args &...args)
        {
            virtualRoot->setDepth(child.depth);
            {
                std::scoped_lock<std::mutex> lck(mtx);
                child.parent = static_cast<Holder *>(roots[threadId]);
                child.root = &roots[threadId];
            }
            virtualRoot->children.push_back(&child);
            linkVirtualRoot_helper(virtualRoot, args...);
        }

        template <typename Holder>
        void prune(Holder *holder)
        {
            //holder.prune();
            holder->root = nullptr;
            holder->parent = nullptr;
        }

        template <typename Holder>
        void lowerRoot(Holder &holder)
        {
            this->assign_root(holder.threadId, &holder);
            holder.parent = nullptr;
        }

        /* this is useful because at level zero of a root, there might be multiple
		waiting nodes, though the leftMost branch (at zero level) might be at one of 
		the very right sub branches deep down, which means that there is a line of
		 multiple nodes with a single child.
		 A node with a single child means that it has already been solved and 
		 also its siblings, because children are unlinked from their parent node
		 when these ones are pushed or fully solved (returned) 
		 							
							root == parent
						  /  |  \   \  \
						 /   |   \	 \	 \
						/    |    \	  \	   \
				leftMost     w1    w2  w3 ... wk
						\
						 *
						  \
						   *
						   	\
						current_level
		
		if there are available threads, and all waiting nodes at level zero are pushed,
		then root should lower down where it finds a node with at least two children or
		the deepest node
		 */
        template <typename Holder>
        void rootCorrecting(Holder *root)
        {
            Holder *_root = root;

            while (_root->children.size() == 1) // lowering the root
            {
                _root = _root->children.front();
                _root->parent->children.pop_front();
                this->lowerRoot(*_root);
            }
        }

        ~DLB_Handler() = default;
        DLB_Handler(const DLB_Handler &) = delete;
        DLB_Handler(DLB_Handler &&) = delete;
        DLB_Handler &operator=(const DLB_Handler &) = delete;
        DLB_Handler &operator=(DLB_Handler &&) = delete;
    };
}
#endif