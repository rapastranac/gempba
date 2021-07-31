#pragma once
#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

/*
* Created by Andres Pastrana on 2019
* pasr1602@usherbrooke.ca
* rapastranac@gmail.com
*/
#include <fmt/format.h>
#include "args_handler.hpp"
#include "DLB_Handler.hpp"
#include "ThreadPool.hpp"

#ifdef MPI_ENABLED
#include <mpi.h>
#include <stdio.h>
#endif

#include <any>
#include <atomic>
#include <bits/stdc++.h>
#include <chrono>
#include <future>
#include <functional>
#include <limits.h>
#include <list>
#include <iostream>
#include <math.h>
#include <mutex>
#include <queue>
#include <sstream>
#include <sys/time.h>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace GemPBA
{
	template <typename _Ret, typename... Args>
	class ResultHolder;

	template <typename _Ret, typename... Args>
	class Emulator;

	class MPI_Scheduler;

	class BranchHandler
	{
		template <typename _Ret, typename... Args>
		friend class GemPBA::ResultHolder;

		template <typename _Ret, typename... Args>
		friend class GemPBA::Emulator;

		friend class MPI_Scheduler;

	protected:
		void add_on_idle_time(std::chrono::steady_clock::time_point begin, std::chrono::steady_clock::time_point end)
		{
			double time_tmp = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
			idleTime.fetch_add(time_tmp, std::memory_order_relaxed);
		}

	public:
		double getPoolIdleTime()
		{
			return thread_pool->idle_time() / (double)processor_count;
		}

		int getPoolSize() //ParBranchHandler::getInstance().MaxThreads() = 10;
		{
			return this->thread_pool->size();
		}

		void initThreadPool(int poolSize)
		{
			this->processor_count = poolSize;
			thread_pool = std::make_unique<ThreadPool::Pool>(poolSize);
		}

		void lock()
		{
			this->mtx.lock();
		}
		void unlock()
		{
			this->mtx.unlock();
		}

		//seconds
		double idle_time()
		{
			double nanoseconds = idleTime / ((double)processor_count + 1);
			return nanoseconds * 1.0e-9; // convert to seconds
		}

		void holdSolution(auto &bestLocalSolution)
		{
			std::unique_lock<std::mutex> lck(mtx);
			this->bestSolution = std::make_any<decltype(bestLocalSolution)>(bestLocalSolution);
		}

		void holdSolution(int refValueLocal, auto &solution, auto &serializer)
		{
			std::unique_lock<std::mutex> lck(mtx);
			this->bestSolution_serialized.first = refValueLocal;
			this->bestSolution_serialized.second = serializer(solution);
		}
		// get number of successful thread requests
		size_t number_thread_requests()
		{
			return numThreadRequests.load();
		}
		// get number for this rank
		int rank_me()
		{
#ifdef MPI_ENABLED
			return mpiScheduler->rank_me();
#else
			return -1; // no multiprocessing enabled
#endif
		}

		/* for void algorithms, this allows to reuse the pool
			it allows to wait for the result of a function being treated by the thread pool

		*/
		void wait()
		{
#ifdef DEBUG_COMMENTS
			fmt::print("Main thread waiting results \n");
#endif
			this->thread_pool->wait();
		}

		// wall time
		double WTime()
		{
			struct timeval time;
			if (gettimeofday(&time, NULL))
			{
				//  Handle error
				return 0;
			}
			return (double)time.tv_sec + (double)time.tv_usec * .000001;
		}

		bool has_result()
		{
			return bestSolution.has_value();
		}

		bool isDone()
		{
			return thread_pool->hasFinished();
		}

		void clear_result()
		{
			bestSolution.reset();
		}

		/* if running in multithreading mode, best solution can directly fetch without 
			any deserialization
		*/
		template <typename RESULT_TYPE>
		[[nodiscard]] auto fetchSolution() -> RESULT_TYPE
		{ // fetching results caught by the library=

			return std::any_cast<RESULT_TYPE>(bestSolution);
		}

		int refValue() const
		{
			return refValueLocal;
		}

		// if multi-processing, then every process should call this method before starting
		void setRefValue(int refValue)
		{
			this->refValueLocal = refValue;
		}

		/*	This method is thread safe:
			
			- returns false if there exists already a better value, this better value is copied
				to the second parameter mostUpToDate if provided by reference

			- return true if successfuly updated*/
		bool updateRefValue(int new_refValue, int *mostUpToDate = nullptr)
		{
			std::scoped_lock<std ::mutex> lck(mtx);
			if ((maximisation && new_refValue > refValueLocal) || (!maximisation && new_refValue < refValueLocal))
			{
				refValueLocal = new_refValue;
				return true;
			}
			else
			{
				if (mostUpToDate)
					*mostUpToDate = refValueLocal;
				return false;
			}
		}

	private:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool
		try_top_holder(F &f, Holder &holder)
		{ // this method should not be possibly accessed if priority (Thread Pool) not acquired
			if (is_DLB)
			{
				Holder *upperHolder = dlb.find_top_holder(&holder);
				if (upperHolder)
				{
					if (upperHolder->isTreated())
						throw std::runtime_error("Attempt to push a treated holder\n");

					if (upperHolder->evaluate_branch_checkIn()) // checks if it's worth it to push
					{
						this->numThreadRequests++;
						upperHolder->setPushStatus();
						std::args_handler::unpack_and_push_void(*thread_pool, f, upperHolder->getArgs());
					}
					else // discard otherwise
					{
						upperHolder->setDiscard();
					}
					return true; // top holder found whether discarded or pushed
				}
				dlb.pop_left_sibling(&holder); // pops holder from parent's children
			}
			return false; // top holder not found or just DLB disabled
		}

#ifdef MPI_ENABLED
		template <typename Holder>
		bool try_top_holder(auto &getBuffer, Holder &holder)
		{ // this method should not be possibly accessed if priority (MPI) not acquired
			if (is_DLB)
			{
				Holder *upperHolder = dlb.find_top_holder(&holder); //  if it finds it, then root has already been lowered
				if (upperHolder)
				{
					if (upperHolder->isTreated())
						throw std::runtime_error("Attempt to push a treated holder\n");

					if (upperHolder->evaluate_branch_checkIn())
					{
						upperHolder->setMPISent(true, mpiScheduler->nextProcess());
						mpiScheduler->push(getBuffer(upperHolder->getArgs()));
					}
					else
					{
						upperHolder->setDiscard();
						// WARNING, ATTENTION, CUIDADO! : holder discarded, flagged as sent but not really sent, then priority should be realeased!!!!
						mpiScheduler->releasePriority();
					}
					return true; // top holder found whether discarded or pushed
				}
				dlb.pop_left_sibling(&holder); // pops holder from parent's children
			}
			return false; // top holder not found
		}

#endif

	public:
		/* Asyncrhonous operation:
		
		Special care should be taken with this method, otherwise deadlocks
		might appear.

		It could be used once for the pushing the first time		
		*/
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		void force_push(F &f, int id, Holder &holder)
		{
			holder.setPushStatus();
			dlb.prune(&holder);
			std::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		void force_push(F &f, int id, Holder &holder)
		{
			holder.setPushStatus();
			dlb.prune(&holder);
			std::args_handler::unpack_and_forward_non_void(f, id, holder.getArgs(), holder);
		}

	private:
		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &&f, int id, Holder &holder)
		{
			/* the underlying loop breaks under one of the following scenarios:
				- mutex cannot be acquired
				- there is no available thread in the pool
				- current level holder is pushed

				NOTE: if top holder found, it'll keep trying to find more
			*/
			while (true)
			{
				std::unique_lock<std::mutex> lck(mtx, std::defer_lock);
				if (lck.try_lock())
				{
					if (thread_pool->n_idle() > 0)
					{

						if (try_top_holder<_ret>(f, holder))
						{
							continue; // keeps iterating from root to current level
						}
						else
						{
							if (holder.isTreated())
								throw std::runtime_error("Attempt to push a treated holder\n");

							//after this line, only leftMost holder should be pushed
							this->numThreadRequests++;
							holder.setPushStatus();
							dlb.prune(&holder);

							std::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
							return true; // pushed to pool
						}
					}
					break; // mutex released at destruction
				}
				else
				{
					break;
				}
			}
			this->forward<_ret>(f, id, holder);
			return false;
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multithreading(F &f, int id, Holder &holder)
		{
			/*This lock must be performed before checking the condition,
			even though numThread is atomic*/
			std::unique_lock<std::mutex> lck(mtx);
			//if (busyThreads < thread_pool->size())
			if (thread_pool->n_idle() > 0)
			{
				if (is_DLB)
				{
					//bool res = try_top_holder<_ret>(lck, f, holder);
					//if (res)
					//	return false; //if top holder found, then it should return false to keep trying

					dlb.pop_left_sibling(&holder);
				}
				this->numThreadRequests++;
				holder.setPushStatus();

				lck.unlock();
				auto ret = std::args_handler::unpack_and_push_non_void(*thread_pool, f, holder.getArgs());
				holder.hold_future(std::move(ret));
				return true;
			}
			else
			{
				lck.unlock();
				if (is_DLB)
				{
					auto ret = this->forward<_ret>(f, id, holder, true);
					holder.hold_actual_result(ret);
				}
				else
				{
					auto ret = this->forward<_ret>(f, id, holder);
					holder.hold_actual_result(ret);
				}
				return true;
			}
		}

	public:
		template <typename _ret, typename F, typename Holder>
		bool try_push_MT(F &&f, int id, Holder &holder)
		{
			return push_multithreading<_ret>(f, id, holder);
		}

#ifdef MPI_ENABLED

		/* 	it attempts pushing on another process by default, if none found,
			it attempts pushing on another thread, if none found
			it will proceed sequentially
		*/
		template <typename _ret, typename F, typename Holder, typename Serializer>
		bool try_push_MP(F &f, int id, Holder &holder, Serializer &&serializer)
		{
			bool _flag = push_multiprocess(id, holder, serializer);

			if (_flag)
				return _flag;
			else
				return try_push_MT<_ret>(f, id, holder);
		}

	private:
		bool push_multiprocess(int id, auto &holder, auto &&serializer)
		{
			/* the underlying loop breaks under one of the following scenarios:
				- unable to acquired priority
				- unable to acquire mutex
				- there is not next available process
				- current level holder is pushed

				NOTE: if top holder found, it'll keep trying to find more
			*/
			while (true)
			{
				std::unique_lock<std::mutex> lck(mtx_MPI, std::defer_lock);
				if (lck.try_lock()) // if mutex acquired, other threads will jump this section
				{
					if (mpiScheduler->acquirePriority())
					{
						auto getBuffer = [&serializer](auto &tuple)
						{
							return std::apply(serializer, tuple);
						};

						if (try_top_holder(getBuffer, holder))
						{
							//if top holder found, then it is pushed, therefore priority is release internally
							continue; // keeps iterating from root to current level
						}
						else // since priority is already acquired, take advantage of it to push current holder
						{
							if (holder.isTreated())
								throw std::runtime_error("Attempt to push a treated holder\n");

							mpiScheduler->push(getBuffer(holder.getArgs())); // this releases priority internally
							holder.setMPISent();
							dlb.prune(&holder);
							return true;
						}
					}
					break;
				}
				else
				{
					break;
				}
			}
			return false;
		}

		template <typename _ret, typename F, typename Holder, typename F_SERIAL>
		bool push_multiprocess(F &f, int id, Holder &holder, F_SERIAL &f_serial, bool)
		{
			bool _flag = false;
			while (!_flag)
				_flag = push_multiprocess<_ret>(f, id, holder, f_serial);

			return _flag;
		}

		template <typename _ret, typename F, typename Holder, typename F_SERIAL,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		bool push_multiprocess(F &f, int id, Holder &holder, F_SERIAL &f_serial)
		{
			int r = try_another_process(holder, f_serial);
			if (r == 0)
				return true;
			if (r == 2)
				return false;

			return push_multithreading<_ret>(f, id, holder);
		}

#endif
	public:
		// no DLB_Handler begin **********************************************************************

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &f, int threadId, Holder &holder)
		{
			// TODO this is related to non-void function on multithreading mode
			// DLB not supported
			holder.setForwardStatus();
			return std::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
		}

		// no DLB_Handler ************************************************************************* end

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		_ret forward(F &f, int threadId, Holder &holder)
		{
			if (holder.isTreated())
				throw std::runtime_error("Attempt to push a treated holder\n");

#ifdef MPI_ENABLED
			if (holder.is_pushed() || holder.is_MPI_Sent())
				return;
#else
			if (holder.is_pushed())
				return;
#endif
			if (is_DLB)
				dlb.checkLeftSibling(&holder); // it checks if root must be moved

			holder.setForwardStatus();
			std::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);
		}

		template <typename _ret, typename F, typename Holder,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &f, int threadId, Holder &holder, bool)
		{
			// TODO this is related to non-void function on multithreading mode
			// in construction, DLB may be supported
			if (holder.is_pushed())
				return holder.get();

			if (is_DLB)
				dlb.checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}

#ifdef MPI_ENABLED
		template <typename _ret, typename F, typename Holder, typename F_DESER,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		_ret forward(F &f, int threadId, Holder &holder, F_DESER &f_deser, bool)
		{
			// TODO this is related to non-void function on multiprocessing mode
			// in construction

			if (holder.is_pushed() || holder.is_MPI_Sent()) //TODO.. this should be considered when using DLB_Handler and pushing to another processsI
				return holder.get(f_deser);					//return {}; // nope, if it was pushed, then result should be retrieved in here

			if (is_DLB)
				dlb.checkLeftSibling(&holder);

			return forward<_ret>(f, threadId, holder);
		}

		/* 	
			types must be passed through the brackets constructBufferDecoder<_Ret, Args...>(..), so it is
			known at compile time.
			
			_Ret: stands for the return type of the main function
			Args...: is the type list of original type of the function, without considering int id, and void* parent
			
			input: this method receives the main algorithm and a deserializer.
			
			return:  a lambda object who is in charge of receiving a raw buffer in MPI_Scheduler::runNode(...), this 
			lambda object will deserialize the buffer and create a new Holder containing
			the deserialized arguments. Lambda object will push to thread pool and it
			will return a pointer to the holder
			*/
		template <typename _Ret, typename... Args>
		[[nodiscard]] auto constructBufferDecoder(auto &&callable, auto &&deserializer)
		{
			return [this, callable, deserializer](const char *buffer, const int count)
			{
				using HolderType = GemPBA::ResultHolder<_Ret, Args...>;
				HolderType *holder = new HolderType(dlb, -1);

				std::stringstream ss;
				for (int i = 0; i < count; i++)
				{
					ss << buffer[i];
				}
				auto _deser = std::bind_front(deserializer, std::ref(ss));
				std::apply(_deser, holder->getArgs());

				force_push<_Ret>(callable, -1, *holder);

				return holder;
			};
		}

		// this returns a lambda function which returns the best results as raw data
		[[nodiscard]] auto constructResultFetcher()
		{
			return [this]()
			{
				if (bestSolution_serialized.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestSolution_serialized;
			};
		}

		// meant to be used with non-void functions
		// in construction
		[[nodiscard]] auto constructResultFetcher(auto *holder, auto &&deserializer)
		{
			return [this]()
			{
				if (bestSolution_serialized.first == -1)
					return std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
				else
					return bestSolution_serialized;
			};
		}

#endif

	private:
		void init()
		{
			this->processor_count = std::thread::hardware_concurrency();
			this->idleTime = 0;
			this->numThreadRequests = 0;
			this->bestSolution_serialized.first = -1; // this allows to avoid sending empty buffers
		}

		std::atomic<size_t> numThreadRequests;

		/*This section refers to the strategy wrapping a function
			then pruning data to be use by the wrapped function<<---*/
		std::any bestSolution;
		std::pair<int, std::string> bestSolution_serialized;

		DLB_Handler &dlb = GemPBA::DLB_Handler::getInstance();
#ifdef R_SEARCH
		bool is_DLB = true; // enables the novel dynamic load balancing
#else
		bool is_DLB = false; // it leaves the greedy or work-stealing dynamic load balancing by default
#endif

		/*------------------------------------------------------>>end*/
		/* "processor_count" would allow to set by default the maximum number of threads
			that the machine can handle unless the user invokes setMaxThreads() */

		unsigned int processor_count;
		std::atomic<long long> idleTime;
		std::mutex mtx; //local mutex
		std::condition_variable cv;
		std::unique_ptr<ThreadPool::Pool> thread_pool;

		BranchHandler()
		{
			init();
		}

	public:
		static BranchHandler &getInstance()
		{
			static BranchHandler instance;
			return instance;
		}

		~BranchHandler()
		{
		}
		BranchHandler(const BranchHandler &) = delete;
		BranchHandler(BranchHandler &&) = delete;
		BranchHandler &operator=(const BranchHandler &) = delete;
		BranchHandler &operator=(BranchHandler &&) = delete;

#ifdef MPI_ENABLED
		// if multiprocessing, BranchHandler should have access to the mpi scheduler
		void passMPIScheduler(MPI_Scheduler *mpiScheduler)
		{
			this->mpiScheduler = mpiScheduler;
		}
#endif

		void setRefValStrategyLookup(std::string keyword)
		{
			// convert string to upper case
			std::for_each(keyword.begin(), keyword.end(), [](char &c)
						  { c = std::toupper(c); });

			if (keyword == "MAXIMISE")
			{
				return; // maximise by default
			}
			else if (keyword == "MINIMISE")
			{
				maximisation = false;
				refValueLocal = INT_MAX;
#ifdef MPI_ENABLED
				mpiScheduler->setRefValStrategyLookup(maximisation); // TODO redundant
#endif
			}
			else
				throw std::runtime_error("in setRefValStrategyLookup(), keyword : " + keyword + " not recognised\n");
		}

		/*----------------Singleton----------------->>end*/
	protected:
		int refValueLocal = INT_MIN;
		bool maximisation = true;

#ifdef MPI_ENABLED

		MPI_Scheduler *mpiScheduler = nullptr;

		std::mutex mtx_MPI;		  // mutex to ensure MPI_THREAD_SERIALIZED
		int world_rank = -1;	  // get the rank of the process
		int world_size = -1;	  // get the number of processes/nodes
		char processor_name[128]; // name of the node

		// this section below should be delegated to MPI_Scheduler

		MPI_Comm *world_Comm = nullptr; // world communicator MPI
		MPI_Comm &getCommunicator()
		{
			return *world_Comm;
		}

		/*	reply methods are under construction: these methodes are meant to hanle
			non-void functions on MPI such that the returned value can be
			forwarded to the process it was generated from
		*/

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<!std::is_void_v<_ret>, int> = 0>
		void reply(Serialize &&serialize, Holder &holder, int src)
		{
			_ret res; // default construction of return type "_ret"
#ifdef DEBUG_COMMENTS
			fmt::print("rank {} entered reply! \n", world_rank);
#endif
			res = holder.get();

			if (src == 0) // termination, since all recursions return to center node
			{
#ifdef DEBUG_COMMENTS
				fmt::print("Cover size() : {}, sending to center \n", res.coverSize());
#endif

				bestSolution_serialized.first = refValue();
				auto &ss = bestSolution_serialized.second; // it should be empty
				serialize(ss, res);
			}
			else // some other node requested help and it is surely waiting for the return value
			{

#ifdef DEBUG_COMMENTS
				fmt::print("rank {} about to reply to {}! \n", world_rank, src);
#endif
				std::unique_lock<std::mutex> lck(mtx_MPI); //no other thread can retrieve nor send via MPI

				std::stringstream ss;
				serialize(ss, res);
				int count = ss.str().size();

				int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, src, 0, *world_Comm);
				if (err != MPI_SUCCESS)
					fmt::print("result could not be sent from rank {} to rank {}! \n", world_rank, src);
			}
		}

		template <typename _ret, typename Holder, typename Serialize,
				  std::enable_if_t<std::is_void_v<_ret>, int> = 0>
		void reply(Serialize &&, Holder &, int)
		{
			thread_pool->wait();
		}

#endif
	};
} // namespace GemPBA

#endif
