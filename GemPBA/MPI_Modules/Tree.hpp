#ifndef TREE_HPP
#define TREE_HPP

#include <iostream>
#include <vector>

class Tree
{
private:
    struct Node
    {
        explicit Node(Tree &tree, int idx) : tree(tree)
        {
            this->idx = idx;
            this->childrenCount = 0;
            this->parent = nullptr;
            this->leftSibling = nullptr;
            this->rightSibling = nullptr;
            this->next = nullptr;
            this->last = nullptr;
            this->dummy = nullptr;
        }

        // this method allows to add a next node,
        void addNext(int idx)
        {
            if (!tree[idx].parent)
            {
                tree[idx].parent = this;
                if (childrenCount == 0)
                {
                    next = &tree[idx];
                }
                else
                {
                    Node *tail = next;
                    while (tail->rightSibling)
                    {
                        tail = tail->rightSibling;
                    }
                    tree[idx].leftSibling = tail;
                    tail->rightSibling = &tree[idx];
                }
                last = &tree[idx];
                childrenCount++;
            }
            else
            {
                std::cerr << "node " << idx << " is already assigned to " << tree[idx].parent->idx << "\n";
                throw;
            }
        }

        // tell if this node is assigned to another one
        bool isAssigned()
        {
            if (parent)
                return true;
            else
                return false;
        }

        bool hasNext()
        {
            if (next)
                return true;
            else
                return false;
        }

        // this method return the next node id, it returns -1 if not available
        int getNext()
        {
            if (next)
                return next->idx;
            else
                return -1;
        }

        int getParent()
        {
            if (parent)
                return parent->idx;
            else
                return -1;
        }

        // this method unlinks and pops out the next node, leaving the second one (if applicable) as the new next
        void pop_front()
        {
            if (next)
            {
                if (next->rightSibling) // at least two nodes
                {
                    auto nextCpy = next;
                    next = next->rightSibling;
                    next->leftSibling = nullptr;

                    nextCpy->parent = nullptr;
                    nextCpy->leftSibling = nullptr;
                    nextCpy->rightSibling = nullptr;
                }
                else // the only available node
                {
                    next->parent = nullptr;
                    next = nullptr;
                    last = nullptr;
                }
                --childrenCount;
            }
            else
            {
                std::cerr << "there's no next to pop\n";
                throw;
            }
        }

        void clear()
        {
            while (next)
            {
                pop_front();
            }
        }

        // realease current node from its parent
        void release()
        {
            if (parent)
            {
                if (leftSibling && rightSibling) // in the midle
                {
                    leftSibling->rightSibling = rightSibling;
                    rightSibling->leftSibling = leftSibling;
                    --(parent->childrenCount);
                    parent = nullptr;
                    rightSibling = nullptr;
                    leftSibling = nullptr;
                }
                else if (leftSibling && !rightSibling) // last one
                {
                    leftSibling->rightSibling = nullptr;
                    parent->last = leftSibling;
                    --(parent->childrenCount);
                    parent = nullptr;
                    leftSibling = nullptr;
                }
                else // the only one or the first one
                {
                    parent->pop_front();
                }
            }
            else
            {
                std::cerr << "node " << idx << " is not assigned to any other node \n";
                throw;
            }
        }

        int size()
        {
            return childrenCount;
        }

        class Iterator
        {
        private:
            Node *_ptr;
            friend class Node;
            explicit Iterator(Node *n) : _ptr(n) {}

        public:
            int &operator*() const
            {
                return _ptr->idx;
            }
            // overload pre increment operator
            Iterator &operator++()
            {
                _ptr = _ptr->rightSibling;
                return *this;
            }
            // overload post increment operator
            Iterator operator++(int)
            {
                Iterator ret = *this;
                ++*(this);
                return ret;
            }

            bool operator==(const Iterator &iter) const
            {
                return this->_ptr == iter._ptr;
            }

            bool operator!=(const Iterator &iter) const
            {
                return this->_ptr != iter._ptr;
            }
        };

        using iterator = Iterator;
        iterator begin() { return iterator(next); }
        iterator end() { return iterator(dummy); }

    private:
        Tree &tree;
        Node *parent;
        Node *leftSibling;
        Node *rightSibling;
        Node *next;
        Node *last;
        Node *dummy;
        int idx = -1;
        int childrenCount;
    };

    std::vector<Node> C;

public:
    Tree() {}
    Tree(size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            C.emplace_back(*this, (int)i);
        }
    }

    void resize(size_t size)
    {
        for (size_t i = 0; i < size; i++)
        {
            C.emplace_back(*this, (int)i);
        }
    }
    size_t size()
    {
        return C.size();
    }

    Node &operator[](int idx)
    {
        return C[idx];
    }

    using iterator = std::vector<Node>::iterator;
    iterator begin() { return C.begin(); }
    iterator end() { return C.end(); }
};

#endif