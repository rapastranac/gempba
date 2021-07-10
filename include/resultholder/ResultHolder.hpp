#ifndef RESULTHOLDER_HPP
#define RESULTHOLDER_HPP

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/

#include "VoidIntermediate.hpp"
#include "NonVoidIntermediate.hpp"

namespace GemPBA
{

	template <typename _Ret, typename... Args>
	class ResultHolder : public ResultHolderInt<_Ret, void, Args...>
	{
		friend class DLB_Handler;

	private:
		void **root = nullptr;				// raw pointer
		ResultHolder *parent = nullptr;		// smart pointer
		ResultHolder *itself = nullptr;		// this;		// raw pointer
		std::list<ResultHolder *> children; // smart pointer, it keeps the order in which they were appended

	public:
		// default constructor, it has no parent, used for virtual roots
		ResultHolder(DLB_Handler &dlb, int threadId) : ResultHolderInt<_Ret, void, Args...>(dlb), Base<Args...>(dlb)
		{
			this->threadId = threadId;
			this->id = dlb.getUniqueId();
			//this->expectedFut.reset(new std::future<_Ret>);
			this->itself = this;

			this->dlb.assign_root(threadId, this);
			this->root = &dlb.roots[threadId];

			this->isVirtual = true;
		}

		ResultHolder(DLB_Handler &dlb, int threadId, void *parent) : ResultHolderInt<_Ret, void, Args...>(dlb), Base<Args...>(dlb)
		{
			this->threadId = threadId;
			this->id = this->dlb.getUniqueId();
			itself = this;

			if (parent)
			{
				this->root = &dlb.roots[threadId];
				this->parent = static_cast<ResultHolder *>(parent);
				this->parent->children.push_back(this);
			}
			else
			{
				// if there is no parent, it means the thread just took another subtree
				// therefore, root in handler.roots[threadId] should change since
				// no one else is supposed to be using it
				this->dlb.assign_root(threadId, this);
				this->root = &dlb.roots[threadId];
				return;
			}
		}

		~ResultHolder()
		{
			//#ifdef DEBUG_COMMENTS
			//			if (this->isVirtual)
			//				fmt::print("Destructor called for virtual root, id : {}, \t threadId :{}, \t depth : {} \n", this->id, this->threadId, this->depth);
			////else
			////	fmt::print("Destructor called for  id : {} \n", this->id);
			//#endif
		}

		ResultHolder(ResultHolder &&src) = delete;
		ResultHolder(ResultHolder &src) = delete;
	};
} // namespace library
#endif
