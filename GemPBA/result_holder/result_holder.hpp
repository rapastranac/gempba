#ifndef RESULTHOLDER_HPP
#define RESULTHOLDER_HPP

#include <result_holder/result_holder_int_non_void.hpp>
#include <result_holder/result_holder_int_void.hpp>

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */
namespace gempba {

    template<typename Ret, typename... Args>
    class result_holder : public result_holder_int<Ret, void, Args...> {
        friend class DLB_Handler;

    private:
        void **root = nullptr; // raw pointer
        result_holder *parent = nullptr; // raw pointer
        result_holder *itself = nullptr; // this;	raw pointer
        std::list<result_holder *> children; // raw pointers, it keeps the order in which they were appended

    public:
        // default constructor, it has no parent, used for virtual roots
        result_holder(DLB_Handler &dlb, int threadId) :
            result_holder_int<Ret, void, Args...>(dlb), result_holder_base<Args...>(dlb) {
            this->threadId = threadId;
            this->id = dlb.getUniqueId();
            this->itself = this;
            this->dlb.assign_root(threadId, this);
            this->root = &dlb.roots[threadId];
            this->isVirtual = true;
        }

        result_holder(DLB_Handler &dlb, int threadId, void *parent) :
            result_holder_int<Ret, void, Args...>(dlb), result_holder_base<Args...>(dlb) {
            this->threadId = threadId;
            this->id = this->dlb.getUniqueId();
            this->itself = this;

            if (parent) {
                this->root = &dlb.roots[threadId];
                this->parent = static_cast<result_holder *>(parent);
                this->parent->children.push_back(this);
            } else {
                // if there is no parent, it means the thread just took another subtree
                // therefore, root in handler.roots[threadId] should change since
                // no one else is supposed to be using it
                this->dlb.assign_root(threadId, this);
                this->root = &dlb.roots[threadId];
            }
        }

        ~result_holder() = default;

        result_holder(result_holder &&src) = delete;

        result_holder(result_holder &src) = delete;
    };
}
#endif
