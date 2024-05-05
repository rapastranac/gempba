#ifndef GEMPBA_NODEMANAGER_H
#define GEMPBA_NODEMANAGER_H

#include <any>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "BranchHandler/ThreadPool.hpp"
#include "BranchHandler/args_handler.hpp"
#include "DLB/DLB_Handler.hpp"
#include "function_trace/api/trace_node.hpp"
#include "utils/utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {
    enum LookupStrategy {
        MAXIMISE, MINIMISE
    };

    enum LoadBalancingStrategy {
        QUASI_HORIZONTAL, // Our Novel Dynamic Load Balancer
        WORK_STEALING
    };

    class NodeManager {
    private:


        std::atomic<size_t> _thread_requests_count = 0;
        std::any _best_result;
        std::optional<std::pair<int, std::string>> _best_serialized_result_optional = std::nullopt;

        DLB_Handler &_dlb = gempba::DLB_Handler::getInstance();
        LoadBalancingStrategy _load_balancing_strategy = QUASI_HORIZONTAL;

        unsigned int _processor_count;
        unsigned int _threads_count = 0;
        std::atomic<long long> _idle_time = 0;
        std::mutex _mtx;
        std::unique_ptr<ThreadPool::Pool> thread_pool = nullptr;

        NodeManager() {
            this->_processor_count = std::thread::hardware_concurrency();
            this->thread_pool = std::make_unique<ThreadPool::Pool>(this->_processor_count);
        }


        /**
      * this method should not possibly be accessed if priority (Thread Pool) is not acquired
      */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        bool try_top_holder(F &f, HolderType &holder) {
            if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                HolderType *upperHolder = _dlb.find_top_holder(&holder);
                if (upperHolder) {
                    if (upperHolder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (upperHolder->evaluate_branch_checkIn()) { // checks if it's worth it to push
                        this->_thread_requests_count++;
                        upperHolder->setPushStatus();
                        gempba::args_handler::unpack_and_push_void(*thread_pool, f, upperHolder->getArgs());
                    } else { // discard otherwise
                        upperHolder->setDiscard();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                _dlb.pop_left_sibling(&holder); // pops holder from parent's children
            }
            return false; // top holder isn't found or just DLB is disabled
        }

        /**
         *  this method should not be possibly accessed if priority (MPI) not acquired
         */
        template<typename HolderType>
        bool try_top_holder(auto &getBuffer, HolderType &holder) {
            if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                HolderType *upperHolder = _dlb.find_top_holder(&holder); //  if it finds it, then the root has already been lowered
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
                _dlb.pop_left_sibling(&holder); // pops holder from parent's children
            }
            return false; // top holder not found
        }



        template<typename R, typename F, typename ...Args, std::enable_if_t<std::is_void_v<R>, int> = 0>
        R forward(F &f, int threadId, TraceNode<Args...> &node) {
            if (node.isConsumed()) {
                throw std::runtime_error("Attempt to push a treated holder\n");
            }

            if (node.getState() == PUSHED || node.getState() == SENT_TO_ANOTHER_PROCESS) {
                return;
            }
            if (node.getState() == PUSHED) {
                return;
            }
            if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                _dlb.checkLeftSibling(&node); // it checks if root must be moved
            }

            node.setState(FORWARDED);
            gempba::args_handler::unpack_and_forward_void(f, threadId, node.getArgs(), &node);
        }

        /**
         * @note this is related to non-void function on multithreading mode. In construction, DLB may be supported
         */
        template<typename R, typename F, typename ...Args, std::enable_if_t<!std::is_void_v<R>, int> = 0>
        R forward(F &f, int threadId, TraceNode<Args...> &node, bool) {
            if (node.getState() == PUSHED) {
                return node.template get<R>();
            }

            if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                _dlb.checkLeftSibling(&node);
            }

            return forward<R>(f, threadId, node);
        }


        template<typename R, typename F, typename ...Args, std::enable_if_t<std::is_void_v<R>, int> = 0>
        bool push_local(F &&f, int id, TraceNode<Args...> &node) {
            /* the underlying loop breaks under one of the following scenarios:
                - mutex cannot be acquired
                - there is no available thread in the pool
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock<std::mutex> lck(_mtx, std::defer_lock);
                if (!lck.try_lock()) {
                    break;
                }

                if (thread_pool->n_idle() > 0) {
                    if (try_top_holder<R>(f, node)) {
                        continue; // keeps iterating from root to current level
                    }

                    if (node.isConsumed()) {
                        throw std::runtime_error("Attempt to push a consumed holder\n");
                    }

                    // after this line, only the leftMost holder should be pushed
                    this->_thread_requests_count++;
                    node.setState(PUSHED);
                    _dlb.prune(&node);

                    gempba::args_handler::unpack_and_push_void(*thread_pool, f, node.getArgs());
                    return true; // pushed to the pool
                }
            }
            this->forward<R>(f, id, node);
            return false;
        }

        template<typename R, typename F, typename ...Args, std::enable_if_t<!std::is_void_v<R>, int> = 0>
        bool push_local(F &f, int id, TraceNode<Args...> &node) {
            /*This lock must be acquired before checking the condition,
            even though numThread is atomic*/
            std::unique_lock<std::mutex> lck(_mtx);
            // if (busyThreads < thread_pool->size())
            if (thread_pool->n_idle() > 0) {
                if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                    // bool res = try_top_holder<Ret>(lck, f, holder);
                    // if (res)
                    //	return false; //if top holder found, then it should return false to keep trying

                    _dlb.pop_left_sibling(&node);
                }
                this->_thread_requests_count++;
                node.setPushStatus();

                lck.unlock();
                auto ret = gempba::args_handler::unpack_and_push_non_void(*thread_pool, f, node.getArgs());
                node.hold_future(std::move(ret));
                return true;
            } else {
                lck.unlock();
                if (_load_balancing_strategy == QUASI_HORIZONTAL) {
                    auto ret = this->forward<R>(f, id, node, true);
                    node.hold_actual_result(ret);
                } else {
                    auto ret = this->forward<R>(f, id, node);
                    node.hold_actual_result(ret);
                }
                return true;
            }
        }

    public:
        static NodeManager &getInstance() {
            static NodeManager instance;
            return instance;
        }

        ~NodeManager() = default;

        NodeManager(const NodeManager &) = delete;

        NodeManager(NodeManager &&) = delete;

        NodeManager &operator=(const NodeManager &) = delete;

        NodeManager &operator=(NodeManager &&) = delete;


        void setThreadsCount(int size) {
            this->_threads_count = size;
            this->thread_pool = std::make_unique<ThreadPool::Pool>(size);
        }

        void holdSolution(auto &best_local_solution) {
            std::unique_lock<std::mutex> lck(_mtx);
            this->_best_result = std::make_any<decltype(best_local_solution)>(best_local_solution);
        }

        void holdSolution(int local_reference_value, auto &solution, auto &serializer) {
            std::unique_lock<std::mutex> lck(_mtx);
            this->_best_serialized_result_optional = std::make_pair(local_reference_value, serializer(solution));
        }


        void wait() {
            utils::print_mpi_debug_comments("Main thread waiting results \n");
            this->thread_pool->wait();
        }

        // wall time
        static double getWallTime() {
            struct timeval time{};
            if (gettimeofday(&time, nullptr)) {
                //  Handle error
                return 0;
            }
            return (double) time.tv_sec + (double) time.tv_usec * .000001;
        }

        /**
                *
                * @tparam R return type
                * @tparam F function type
                * @tparam HolderType ResultHolder type
                * @param f function to be execute
                * @param node ResultHolder instance that wraps the function arguments and potential result
                * @return
                */
        template<typename R, typename F, typename ...Args>
        bool try_push_local(F &&f, int id, TraceNode<Args...> &node) {
            return push_local<R>(f, id, node);
        }

#ifdef MULTIPROCESSING_ENABLED
    private:

    public:

#endif
    };
}


#endif //GEMPBA_NODEMANAGER_H
