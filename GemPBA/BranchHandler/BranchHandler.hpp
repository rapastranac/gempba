#pragma once
#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

#include <fmt/format.h>
#include "args_handler.hpp"
#include <DLB/DLB_Handler.hpp>
#include "ThreadPool.hpp"
#include "utils/utils.hpp"

#ifdef MPI_ENABLED

#include <mpi.h>
#include <cstdio>

#endif

#include <any>
#include <atomic>
#include <bits/stdc++.h>
#include <chrono>
#include <future>
#include <functional>
#include <climits>
#include <list>
#include <iostream>
#include <cmath>
#include <mutex>
#include <queue>
#include <sstream>
#include <sys/time.h>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <thread>

/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */
namespace gempba {

    enum LookupStrategy {
        MAXIMISE, MINIMISE
    };

    enum LoadBalancingStrategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };

    template<typename Ret, typename... Args>
    class ResultHolder;

    class MPI_Scheduler;

    class BranchHandler {

        template<typename Ret, typename... Args>
        friend
        class ResultHolder;

        friend class MPI_Scheduler;

    private:
        const std::pair<int, std::string> EMPTY_RESULT = std::make_pair(0, static_cast<std::string>("Empty buffer, no result"));
        std::atomic<size_t> numThreadRequests;
        /*This section refers to the strategy wrapping a function
            then pruning data to be used by the wrapped function<<---*/
        std::any bestSolution;
        std::pair<int, std::string> bestSolution_serialized;

        DLB_Handler &dlb = gempba::DLB_Handler::getInstance();
        LoadBalancingStrategy _loadBalancingStrategy = QUASI_HORIZONTAL;

        int refValueLocal = INT_MIN;
        bool maximisation = true;

        /*------------------------------------------------------>>end*/
        /* "processor_count" would allow setting by default the maximum number of threads
            that the machine can handle unless the user invokes setMaxThreads() */

        unsigned int processor_count;
        std::atomic<long long> idleTime;
        std::mutex mtx; // local mutex
        std::condition_variable cv;
        std::unique_ptr<ThreadPool::Pool> thread_pool;

        BranchHandler() {
            processor_count = std::thread::hardware_concurrency();
            idleTime = 0;
            numThreadRequests = 0;
            bestSolution_serialized.first = -1; // this allows avoiding sending empty buffers
        }


        /**
      * this method should not possibly be accessed if priority (Thread Pool) is not acquired
      */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        bool try_top_holder(F &f, HolderType &holder) {
            if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                HolderType *upperHolder = dlb.find_top_holder(&holder);
                if (upperHolder) {
                    if (upperHolder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (upperHolder->evaluate_branch_checkIn()) { // checks if it's worth it to push
                        this->numThreadRequests++;
                        upperHolder->setPushStatus();
                        gempba::args_handler::unpack_and_push_void(*thread_pool, f, upperHolder->getArgs());
                    } else { // discard otherwise
                        upperHolder->setDiscard();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                dlb.pop_left_sibling(&holder); // pops holder from parent's children
            }
            return false; // top holder isn't found or just DLB is disabled
        }

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        bool push_multithreading(F &&f, int id, HolderType &holder) {
            /* the underlying loop breaks under one of the following scenarios:
                - mutex cannot be acquired
                - there is no available thread in the pool
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock<std::mutex> lck(mtx, std::defer_lock);
                if (lck.try_lock()) {
                    if (thread_pool->n_idle() > 0) {

                        if (try_top_holder<Ret>(f, holder)) {
                            continue; // keeps iterating from root to current level
                        } else {
                            if (holder.isTreated())
                                throw std::runtime_error("Attempt to push a treated holder\n");

                            // after this line, only leftMost holder should be pushed
                            this->numThreadRequests++;
                            holder.setPushStatus();
                            dlb.prune(&holder);

                            gempba::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
                            return true; // pushed to the pool
                        }
                    }
                    break; // mutex released at destruction
                } else {
                    break;
                }
            }
            this->forward<Ret>(f, id, holder);
            return false;
        }

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        bool push_multithreading(F &f, int id, HolderType &holder) {
            /*This lock must be acquired before checking the condition,
            even though numThread is atomic*/
            std::unique_lock<std::mutex> lck(mtx);
            // if (busyThreads < thread_pool->size())
            if (thread_pool->n_idle() > 0) {
                if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                    // bool res = try_top_holder<Ret>(lck, f, holder);
                    // if (res)
                    //	return false; //if top holder found, then it should return false to keep trying

                    dlb.pop_left_sibling(&holder);
                }
                this->numThreadRequests++;
                holder.setPushStatus();

                lck.unlock();
                auto ret = gempba::args_handler::unpack_and_push_non_void(*thread_pool, f, holder.getArgs());
                holder.hold_future(std::move(ret));
                return true;
            } else {
                lck.unlock();
                if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                    auto ret = this->forward<Ret>(f, id, holder, true);
                    holder.hold_actual_result(ret);
                } else {
                    auto ret = this->forward<Ret>(f, id, holder);
                    holder.hold_actual_result(ret);
                }
                return true;
            }
        }


    public:

        //<editor-fold desc="Construction/Destruction">
        static BranchHandler &getInstance() {
            static BranchHandler instance;
            return instance;
        }

        ~BranchHandler() = default;

        BranchHandler(const BranchHandler &) = delete;

        BranchHandler(BranchHandler &&) = delete;

        BranchHandler &operator=(const BranchHandler &) = delete;

        BranchHandler &operator=(BranchHandler &&) = delete;
        //</editor-fold>

        void setLoadBalancingStrategy(LoadBalancingStrategy strategy) {
            this->_loadBalancingStrategy = strategy;
        };

        LoadBalancingStrategy getLoadBalancingStrategy() const {
            return _loadBalancingStrategy;
        }

        double getPoolIdleTime() {
            return thread_pool->idle_time() / (double) processor_count;
        }

        int getPoolSize() const {
            return (int) this->thread_pool->size();
        }

        void initThreadPool(int poolSize) {
            this->processor_count = poolSize;
            thread_pool = std::make_unique<ThreadPool::Pool>(poolSize);
        }

        void lock() {
            this->mtx.lock();
        }

        void unlock() {
            this->mtx.unlock();
        }

        // seconds
        double idle_time() {
            double nanoseconds = (double) idleTime / ((double) processor_count + 1.0);
            return (double) nanoseconds * 1.0e-9; // convert to seconds
        }

        void holdSolution(auto &bestLocalSolution) {
            std::unique_lock<std::mutex> lck(mtx);
            this->bestSolution = std::make_any<decltype(bestLocalSolution)>(bestLocalSolution);
        }

        void holdSolution(int refValueLocal, auto &solution, auto &serializer) {
            std::unique_lock<std::mutex> lck(mtx);
            this->bestSolution_serialized.first = refValueLocal;
            this->bestSolution_serialized.second = serializer(solution);
        }

        // get number of successful thread requests
        size_t number_thread_requests() {
            return numThreadRequests.load();
        }

        void setLookupStrategy(LookupStrategy strategy) {
            switch (strategy) {
                case MAXIMISE: {
                    return; // maximise by default
                }
                case MINIMISE: {
                    maximisation = false;
                    refValueLocal = INT_MAX;
#ifdef MPI_ENABLED
                    mpiScheduler->setRefValStrategyLookup(maximisation); // TODO redundant
#endif
                }
            };

        }

        // get number for this rank
        int rank_me() {
#ifdef MPI_ENABLED
            return mpiScheduler->rank_me();
#else
            return -1; // no multiprocessing enabled
#endif
        }

        /**
        * Waits for tasks in the thread pool to complete.
        * Useful for void algorithms, allowing reuse of the pool.
        * Blocks the current thread until all tasks finish.
        * Provides synchronization with the main thread.
        */
        void wait() {
            utils::print_mpi_debug_comments("Main thread waiting results \n");
            this->thread_pool->wait();
        }

        // wall time
        static double WTime() {
            struct timeval time{};
            if (gettimeofday(&time, NULL)) {
                //  Handle error
                return 0;
            }
            return (double) time.tv_sec + (double) time.tv_usec * .000001;
        }

        bool has_result() {
            return bestSolution.has_value();
        }

        bool isDone() {
            return thread_pool->hasFinished();
        }

        void clear_result() {
            bestSolution.reset();
        }


        /**
         * if running in multithreading mode, the best solution can directly fetch without any deserialization
         * @tparam SolutionType
         */
        template<typename SolutionType>
        [[nodiscard]] auto fetchSolution() -> SolutionType { // fetching results caught by the library=

            return std::any_cast<SolutionType>(bestSolution);
        }

        int refValue() const {
            return refValueLocal;
        }

        /**
         * if multiprocessing is used, then every process should call this method before starting
         * @param refValue first approximation of the best reference value of the solution
         */
        void setRefValue(int refValue) {
            this->refValueLocal = refValue;
        }

        /**
        * This method is thread safe: Updates the reference value and optionally retrieves the most up-to-date value.
        *
        * @param new_refValue the most promising new reference value for the solution in the scope calling this method
        * @param mostUpToDate A pointer to an integer where the most up-to-date value will be stored.
        * If nullptr, the most up-to-date value is not retrieved.
        * @return True if the reference value was successfully updated, false otherwise.
        */
        bool updateRefValue(int new_refValue, int *mostUpToDate = nullptr) {
            std::scoped_lock<std::mutex> lck(mtx);

            bool isRefValueIncreasing = maximisation && new_refValue > refValueLocal;
            bool isRefValueDecreasing = !maximisation && new_refValue < refValueLocal;
            if (isRefValueIncreasing || isRefValueDecreasing) {
                refValueLocal = new_refValue;
                return true;
            }

            if (mostUpToDate) {
                *mostUpToDate = refValueLocal;
            }
            return false;
        }


        /**
         * Asynchronous operation:
         * Special care should be taken with this method, otherwise deadlocks might occur.
         * It could be used once for pushing the first time.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType
         * @param f function to be execute
         * @param holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        void force_push(F &f, int id, HolderType &holder) {
            holder.setPushStatus();
            dlb.prune(&holder);
            gempba::args_handler::unpack_and_push_void(*thread_pool, f, holder.getArgs());
        }

        /**
         * Asynchronous operation:
         * Special care should be taken with this method, otherwise deadlocks might occur.
         * It could be used once for pushing the first time.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param f function to be execute
         * @param holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        void force_push(F &f, int id, HolderType &holder) {
            holder.setPushStatus();
            dlb.prune(&holder);
            gempba::args_handler::unpack_and_forward_non_void(f, id, holder.getArgs(), holder);
        }

        /**
         *
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param f function to be execute
         * @param holder ResultHolder instance that wraps the function arguments and potential result
         * @return
         */
        template<typename Ret, typename F, typename HolderType>
        bool try_push_MT(F &&f, int id, HolderType &holder) {
            return push_multithreading<Ret>(f, id, holder);
        }

    public:
        // no DLB_Handler begin **********************************************************************

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &f, int threadId, HolderType &holder) {
            // TODO this is related to non-void function on multithreading mode
            // DLB not supported
            holder.setForwardStatus();
            return gempba::args_handler::unpack_and_forward_non_void(f, threadId, holder.getArgs(), &holder);
        }

        // no DLB_Handler ************************************************************************* end

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        Ret forward(F &f, int threadId, HolderType &holder) {
            if (holder.isTreated()) {
                throw std::runtime_error("Attempt to push a treated holder\n");
            }

#ifdef MPI_ENABLED
            if (holder.is_pushed() || holder.is_MPI_Sent())
                return;
#else
            if (holder.is_pushed())
                return;
#endif
            if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                dlb.checkLeftSibling(&holder); // it checks if root must be moved
            }

            holder.setForwardStatus();
            gempba::args_handler::unpack_and_forward_void(f, threadId, holder.getArgs(), &holder);
        }

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &f, int threadId, HolderType &holder, bool) {
            // TODO this is related to non-void function on multithreading mode
            // in construction, DLB may be supported
            if (holder.is_pushed()) {
                return holder.get();
            }

            if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                dlb.checkLeftSibling(&holder);
            }

            return forward<Ret>(f, threadId, holder);
        }


    public:


#ifdef MPI_ENABLED
    private:
        gempba::MPI_Scheduler *mpiScheduler = nullptr;
        std::mutex mtx_MPI;             // mutex to ensure MPI_THREAD_SERIALIZED
        int world_rank = -1;            // get the rank of the process
        int world_size = -1;            // get the number of processes/nodes
        char processor_name[128]{};       // name of the node
        MPI_Comm *world_Comm = nullptr; // world communicator MPI


        bool push_multiprocess(int id, auto &holder, auto &&serializer) {
            /* the underlying loop breaks under one of the following scenarios:
                - unable to acquire priority
                - unable to acquire mutex
                - there is not next available process
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock<std::mutex> lck(mtx_MPI, std::defer_lock);
                if (lck.try_lock()) {
                    // if mutex acquired, other threads will jump this section
                    if (mpiScheduler->openSendingChannel()) {
                        auto getBuffer = [&serializer](auto &tuple) {
                            return std::apply(serializer, tuple);
                        };

                        if (try_top_holder(getBuffer, holder)) {
                            // if top holder found, then it is pushed; therefore, priority is release internally
                            continue; // keeps iterating from root to current level
                        } else { // since priority is already acquired, take advantage of it to push the current holder
                            if (holder.isTreated()) {
                                throw std::runtime_error("Attempt to push a treated holder\n");
                            }

                            mpiScheduler->push(getBuffer(holder.getArgs())); // this closes the sending channel internally
                            holder.setMPISent();
                            dlb.prune(&holder);
                            return true;
                        }
                    }
                    break;
                } else {
                    break;
                }
            }
            return false;
        }

        template<typename Ret, typename F, typename HolderType, typename F_SERIAL>
        bool push_multiprocess(F &f, int id, HolderType &holder, F_SERIAL &f_serial, bool) {
            bool isSuccess = false;
            while (!isSuccess) {
                isSuccess = push_multiprocess<Ret>(f, id, holder, f_serial);
            }

            return isSuccess;
        }

        template<typename Ret, typename F, typename HolderType, typename F_SERIAL, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        bool push_multiprocess(F &f, int id, HolderType &holder, F_SERIAL &f_serial) {
            int r = try_another_process(holder, f_serial);  // TODO .. this method does not exist, maybe remove!
            if (r == 0) {
                return true;
            }
            if (r == 2) {
                return false;
            }

            return push_multithreading<Ret>(f, id, holder);
        }

    public:
        MPI_Comm &getCommunicator() {
            return *world_Comm;
        }

        // if multiprocessing, BranchHandler should have access to the mpi scheduler
        void passMPIScheduler(MPI_Scheduler *mpiScheduler) {
            this->mpiScheduler = mpiScheduler;
            this->world_rank = this->mpiScheduler->rank_me();
        }

        int getWorldRank() const {
            return world_rank;
        }


        //<editor-fold desc="In construction... non-void  functions">
        template<typename Ret, typename HolderType, typename Serialize, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        void reply(Serialize &&serialize, HolderType &holder, int src) {
            utils::print_mpi_debug_comments("rank {} entered reply! \n", world_rank);
            // default construction of a return type "Ret"
            Ret res; // TODO .. why in separate lines?
            res = holder.get();

            // termination, since all recursions return to center node
            if (src == 0) {
                utils::print_mpi_debug_comments("cover size() : {}, sending to center \n", res.coverSize());

                bestSolution_serialized.first = refValue();
                auto &ss = bestSolution_serialized.second; // it should be empty
                serialize(ss, res);
            } else {
                // some other node requested help, and it is surely waiting for the return value

                utils::print_mpi_debug_comments("rank {} about to reply to {}! \n", world_rank, src);
                std::unique_lock<std::mutex> lck(mtx_MPI); // no other thread can retrieve nor send via MPI

                std::stringstream ss;
                serialize(ss, res);
                int count = ss.str().size();

                int err = MPI_Ssend(ss.str().data(), count, MPI_CHAR, src, 0, *world_Comm);
                if (err != MPI_SUCCESS) {
                    fmt::print("result could not be sent from rank {} to rank {}! \n", world_rank, src);
                }
            }
        }

        template<typename Ret, typename HolderType, typename Serialize, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        void reply(Serialize &&, HolderType &, int) {
            thread_pool->wait();
        }
        //</editor-fold>



        /**
         *  this method should not be possibly accessed if priority (MPI) not acquired
         */
        template<typename HolderType>
        bool try_top_holder(auto &getBuffer, HolderType &holder) {
            if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                HolderType *upperHolder = dlb.find_top_holder(&holder); //  if it finds it, then the root has already been lowered
                if (upperHolder) {
                    if (upperHolder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (upperHolder->evaluate_branch_checkIn()) {
                        upperHolder->setMPISent(true, mpiScheduler->nextProcess());
                        mpiScheduler->push(getBuffer(upperHolder->getArgs()));
                    } else {
                        upperHolder->setDiscard();
                        // WARNING, ATTENTION, CUIDADO! holder discarded, flagged as sent but not really sent, then sendingChannel should be released!!!!
                        mpiScheduler->closeSendingChannel();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                dlb.pop_left_sibling(&holder); // pops holder from parent's children
            }
            return false; // top holder not found
        }


        /* 	it attempts pushing on another process by default, if none found,
            it attempts to push on another thread, if none found
            it will proceed sequentially
        */
        template<typename Ret, typename F, typename HolderType, typename Serializer>
        bool try_push_MP(F &f, int id, HolderType &holder, Serializer &&serializer) {
            bool isSuccess = push_multiprocess(id, holder, serializer);
            return isSuccess ? isSuccess : try_push_MT<Ret>(f, id, holder);
        }

        template<typename Ret, typename F, typename HolderType, typename Deserializer, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &f, int threadId, HolderType &holder, Deserializer &deserializer, bool) {
            // TODO this is related to non-void function on multiprocessing mode
            // in construction

            if (holder.is_pushed() || holder.is_MPI_Sent()) {
                // TODO.. this should be considered when using DLB_Handler and pushing to another process
                return holder.get(deserializer);
                // return {}; // nope, if it was pushed, then the result should be retrieved in here
            }

            if (_loadBalancingStrategy == QUASI_HORIZONTAL) {
                dlb.checkLeftSibling(&holder);
            }

            return forward<Ret>(f, threadId, holder);
        }

        /*
            Types must be passed through the brackets constructBufferDecoder<Ret, Args...>(..), so it is
            known at compile time.

            Ret: stands for the return type of the main function
            Args...: is the type list of the original type of the function, without considering int id, and void* parent

            input: this method receives the main algorithm and a deserializer.

            return:  a lambda object who is in charge of receiving a raw buffer in MPI_Scheduler::runNode(...), this
            lambda object will deserialize the buffer and create a new Holder containing
            the deserialized arguments.
            Lambda object will push to the thread pool, and it will return a pointer to the holder
            */
        template<typename Ret, typename... Args>
        [[nodiscard]] auto constructBufferDecoder(auto &&callable, auto &&deserializer) {
            using HolderType = gempba::ResultHolder<Ret, Args...>;
            return [this, callable, deserializer](const char *buffer, const int count) {
                auto *holder = new HolderType(dlb, -1);

                std::stringstream ss;
                for (int i = 0; i < count; i++) {
                    ss << buffer[i];
                }
                auto _deserializer = std::bind_front(deserializer, std::ref(ss));
                std::apply(_deserializer, holder->getArgs());

                force_push<Ret>(callable, -1, *holder);

                return holder;
            };
        }


        // this returns a lambda function which returns the best results as raw data
        [[nodiscard]] std::function<std::pair<int, std::string>()> constructResultFetcher() {
            return [this]() {
                if (bestSolution_serialized.first == -1) {
                    return EMPTY_RESULT;
                } else {
                    return bestSolution_serialized;
                }
            };
        }

        // meant to be used with non-void functions
        [[maybe_unused]] auto constructResultFetcher(auto *holder, auto &&deserializer) {
            return [this]() {
                throw std::runtime_error("Not yet implemented");
            };
        }


#endif
    };
}

#endif
