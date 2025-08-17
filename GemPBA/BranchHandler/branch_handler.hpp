#ifndef BRANCHHANDLER_H
#define BRANCHHANDLER_H

#include <any>
#include <atomic>
#include <climits>
#include <cmath>
#include <functional>
#include <list>
#include <mutex>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <bits/stdc++.h>
#include <sys/time.h>

#include <BranchHandler/ThreadPool.hpp>
#include <BranchHandler/args_handler.hpp>
#include <DLB/DLB_Handler.hpp>
#include <MPI_Modules/mpi_scheduler.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/utils.hpp>


#if GEMPBA_MULTIPROCESSING

#include <cstdio>
#include <mpi.h>

#endif


/*
 * Created by Andres Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */
namespace gempba {


    template<typename Ret, typename... Args>
    class ResultHolder;

    class scheduler_parent;

    class branch_handler {

        template<typename Ret, typename... Args>
        friend
        class ResultHolder;

    private:
        std::atomic<size_t> m_thread_requests;
        /*This section refers to the strategy wrapping a function
            then pruning data to be used by the wrapped function<<---*/
        std::any m_best_solution;
        result m_best_solution_serialized;

        DLB_Handler &m_load_balancer = DLB_Handler::getInstance();
        load_balancing_strategy m_load_balancing_strategy = QUASI_HORIZONTAL;

        score m_score = score::make(INT_MIN);
        goal m_goal = MAXIMISE;

        /*------------------------------------------------------>>end*/
        /* "processor_count" would allow setting by default the maximum number of threads
            that the machine can handle unless the user invokes setMaxThreads() */

        unsigned int m_processor_count;
        std::atomic<long long> m_idle_time;
        std::mutex m_mutex; // local mutex
        std::unique_ptr<ThreadPool::Pool> m_thread_pool;

        branch_handler() :
            m_best_solution_serialized(result::EMPTY) {

            m_processor_count = std::thread::hardware_concurrency();
            m_idle_time = 0;
            m_thread_requests = 0;
        }


        /**
      * this method should not possibly be accessed if priority (Thread Pool) is not acquired
      */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        bool try_top_holder(F &f, HolderType &holder) {
            if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                HolderType *upperHolder = m_load_balancer.find_top_holder(&holder);
                if (upperHolder) {
                    if (upperHolder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (upperHolder->evaluate_branch_checkIn()) {
                        // checks if it's worth it to push
                        this->m_thread_requests++;
                        upperHolder->setPushStatus();
                        gempba::args_handler::unpack_and_push_void(*m_thread_pool, f, upperHolder->getArgs());
                    } else {
                        // discard otherwise
                        upperHolder->setDiscard();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                m_load_balancer.pop_left_sibling(&holder); // pops holder from parent's children
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
                std::unique_lock<std::mutex> lck(m_mutex, std::defer_lock);
                if (lck.try_lock()) {
                    if (m_thread_pool->n_idle() > 0) {

                        if (try_top_holder<Ret>(f, holder)) {
                            continue; // keeps iterating from root to current level
                        } else {
                            if (holder.isTreated())
                                throw std::runtime_error("Attempt to push a treated holder\n");

                            // after this line, only leftMost holder should be pushed
                            this->m_thread_requests++;
                            holder.setPushStatus();
                            m_load_balancer.prune(&holder);

                            gempba::args_handler::unpack_and_push_void(*m_thread_pool, f, holder.getArgs());
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
            std::unique_lock<std::mutex> lck(m_mutex);
            // if (busyThreads < thread_pool->size())
            if (m_thread_pool->n_idle() > 0) {
                if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                    // bool res = try_top_holder<Ret>(lck, f, holder);
                    // if (res)
                    //	return false; //if top holder found, then it should return false to keep trying

                    m_load_balancer.pop_left_sibling(&holder);
                }
                this->m_thread_requests++;
                holder.setPushStatus();

                lck.unlock();
                auto ret = gempba::args_handler::unpack_and_push_non_void(*m_thread_pool, f, holder.getArgs());
                holder.hold_future(std::move(ret));
                return true;
            } else {
                lck.unlock();
                if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
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
        static branch_handler &get_instance() {
            static branch_handler instance;
            return instance;
        }

        ~branch_handler() = default;

        branch_handler(const branch_handler &) = delete;

        branch_handler(branch_handler &&) = delete;

        branch_handler &operator=(const branch_handler &) = delete;

        branch_handler &operator=(branch_handler &&) = delete;

        //</editor-fold>

        void set_load_balancing_strategy(load_balancing_strategy strategy) {
            this->m_load_balancing_strategy = strategy;
        };

        load_balancing_strategy get_load_balancing_strategy() const {
            return m_load_balancing_strategy;
        }

        double get_pool_idle_time() const {
            return m_thread_pool->idle_time() / (double) m_processor_count;
        }

        int get_pool_size() const {
            return (int) this->m_thread_pool->size();
        }

        void init_thread_pool(int p_pool_size) {
            this->m_processor_count = p_pool_size;
            m_thread_pool = std::make_unique<ThreadPool::Pool>(p_pool_size);
        }

        void lock() {
            this->m_mutex.lock();
        }

        void unlock() {
            this->m_mutex.unlock();
        }

        // seconds
        double idle_time() const {
            const double v_nanoseconds = static_cast<double>(m_idle_time) / (static_cast<double>(m_processor_count) + 1.0);
            return v_nanoseconds * 1.0e-9; // convert to seconds
        }

        /**
         * Updates the most up-to-date result. This method overrides the previous result without any checks, and should be used only in multithreading mode. (Not multiprocessing)
         * @tparam T Type of the result
         * @param p_new_result the most promising new result for the solution in the scope calling this method
         * @param p_new_reference_value the most promising new reference value that represents the result
        * @return True if the result was successfully updated, false otherwise.
         */
        template<typename T>
        bool try_update_result(T &p_new_result, const int p_new_reference_value) {
            std::unique_lock v_lock(m_mutex);
            const score v_score_temp = score::make(p_new_reference_value);
            if (!should_update_result(v_score_temp)) {
                return false;
            }

            this->m_best_solution = std::make_any<decltype(p_new_result)>(p_new_result);
            this->m_score = v_score_temp;
            return true;
        }

        /**
         * This method is thread safe: Updates the most up-to-date result if the new reference value is better than the current one. This should be used in multiprocessing mode because it
         * uses a serializer to convert the result into bytes.
         *
         * @tparam T Type of the result
         * @param p_new_result the most promising new result for the solution in the scope calling this method
         * @param p_new_reference_value the most promising new reference value that represents the result
         * @param p_serializer a function that serializes the result into a string
         * @return True if the result was successfully updated, false otherwise.
         */
        template<typename T>
        bool try_update_result(T &p_new_result, int p_new_reference_value, std::function<task_packet(T &)> &p_serializer) {
            std::unique_lock v_lock(m_mutex);
            const score v_score_temp = score::make(p_new_reference_value);

            if (!should_update_result(v_score_temp)) {
                return false;
            }

            const auto v_packet = static_cast<task_packet>(p_serializer(p_new_result));
            this->m_best_solution_serialized = {v_score_temp, v_packet};
            this->m_score = v_score_temp;

            return true;
        }

        /**
        * Warning: This is not meant to be used directly by the user. This is called only by the scheduler.
        *
        * This method is thread safe: Updates the most up-to-date reference value if the new reference value is better than the current one, and clears any previous result.
        *
        * @param p_new_ref_value the most promising new reference value for the solution in the scope calling this method
        * @return True if the reference value was successfully updated, false otherwise.
        */
        bool try_update_reference_value_and_invalidate_result(const int p_new_ref_value) {
            std::scoped_lock<std::mutex> v_lock(m_mutex);
            const score v_score_temp = score::make(p_new_ref_value);

            if (should_update_result(v_score_temp)) {
                m_score = v_score_temp;
                clear_result();
                return true;
            }

            return false;
        }

        // get number of successful thread requests
        size_t number_thread_requests() const {
            return m_thread_requests.load();
        }

        void set_goal(const goal p_goal) {
            m_goal = p_goal;
            switch (p_goal) {
                case MAXIMISE: {
                    return; // maximise by default
                }
                case MINIMISE: {
                    m_score = score::make(INT_MAX);
                    #if GEMPBA_MULTIPROCESSING
                    m_mpi_scheduler->set_goal(MINIMISE, score_type::I32);
                    #endif
                }
            };

        }

        // get number for this rank
        int rank_me() {
            #if GEMPBA_MULTIPROCESSING
            return m_mpi_scheduler->rank_me();
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
            this->m_thread_pool->wait();
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

        bool has_result() const {
            return m_best_solution.has_value();
        }

        bool is_done() const {
            return m_thread_pool->hasFinished();
        }

        void clear_result() {
            m_best_solution.reset();
            m_best_solution_serialized = result::EMPTY;
        }


        /**
         * if running in multithreading mode, the best solution can directly fetch without any deserialization
         * @tparam SolutionType
         */
        template<typename SolutionType>
        [[nodiscard]] auto fetch_solution() -> SolutionType {
            // fetching results caught by the library=

            return std::any_cast<SolutionType>(m_best_solution);
        }

        int reference_value() const {
            return m_score.get_loose<int>();
        }

        /**
         * if multiprocessing is used, then every process should call this method before starting
         * @param p_reference_value first approximation of the best reference value of the solution
         */
        void set_reference_value(const int p_reference_value) {
            this->m_score = score::make(p_reference_value);
        }


        /**
         * Asynchronous operation:
         * Special care should be taken with this method, otherwise deadlocks might occur.
         * It could be used once for pushing the first time.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType
         * @param p_function function to be executed
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        void force_push(F &p_function, int p_id, HolderType &p_holder) {
            p_holder.setPushStatus();
            m_load_balancer.prune(&p_holder);
            gempba::args_handler::unpack_and_push_void(*m_thread_pool, p_function, p_holder.getArgs());
        }

        /**
         * Asynchronous operation:
         * Special care should be taken with this method, otherwise deadlocks might occur.
         * It could be used once for pushing the first time.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        void force_push(F &p_function, int p_id, HolderType &p_holder) {
            p_holder.setPushStatus();
            m_load_balancer.prune(&p_holder);
            gempba::args_handler::unpack_and_forward_non_void(p_function, p_id, p_holder.getArgs(), p_holder);
        }

        /**
         *
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         * @return
         */
        template<typename Ret, typename F, typename HolderType>
        bool try_push_mt(F &&p_function, int p_id, HolderType &p_holder) {
            return push_multithreading<Ret>(p_function, p_id, p_holder);
        }

    public:
        // no DLB_Handler begin **********************************************************************

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &p_function, int p_thread_id, HolderType &p_holder) {
            // TODO this is related to non-void function on multithreading mode
            // DLB not supported
            p_holder.setForwardStatus();
            return gempba::args_handler::unpack_and_forward_non_void(p_function, p_thread_id, p_holder.getArgs(), &p_holder);
        }

        // no DLB_Handler ************************************************************************* end

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        Ret forward(F &p_function, int p_thread_id, HolderType &p_holder) {
            if (p_holder.isTreated()) {
                throw std::runtime_error("Attempt to push a treated holder\n");
            }

            #if GEMPBA_MULTIPROCESSING
            if (p_holder.is_pushed() || p_holder.is_MPI_Sent())
                return;
            #else
            if (p_holder.is_pushed())
                return;
            #endif
            if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder); // it checks if root must be moved
            }

            p_holder.setForwardStatus();
            gempba::args_handler::unpack_and_forward_void(p_function, p_thread_id, p_holder.getArgs(), &p_holder);
        }

        template<typename Ret, typename F, typename HolderType, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &p_function, int p_thread_id, HolderType &p_holder, bool) {
            // TODO this is related to non-void function on multithreading mode
            // in construction, DLB may be supported
            if (p_holder.is_pushed()) {
                return p_holder.get();
            }

            if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder);
            }

            return forward<Ret>(p_function, p_thread_id, p_holder);
        }

    public:
        #if GEMPBA_MULTIPROCESSING

    private:
        scheduler_parent *m_mpi_scheduler = nullptr;
        std::mutex m_mpi_mutex; // mutex to ensure MPI_THREAD_SERIALIZED
        int m_world_rank = -1; // get the rank of the process
        int m_world_size = -1; // get the number of processes/nodes
        char m_processor_name[128]{}; // name of the node
        MPI_Comm *m_world_communicator = nullptr; // world communicator MPI


        bool push_multiprocess(int p_id, auto &p_holder, auto &&p_serializer) {
            /* the underlying loop breaks under one of the following scenarios:
                - unable to acquire priority
                - unable to acquire mutex
                - there is not next available process
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock<std::mutex> lck(m_mpi_mutex, std::defer_lock);
                if (lck.try_lock()) {
                    // if mutex acquired, other threads will jump this section
                    if (m_mpi_scheduler->open_sending_channel()) {
                        auto getBuffer = [&p_serializer](auto &tuple) {
                            return std::apply(p_serializer, tuple);
                        };

                        if (try_top_holder(getBuffer, p_holder)) {
                            // if top holder found, then it is pushed; therefore, priority is release internally
                            continue; // keeps iterating from root to current level
                        } else {
                            // since priority is already acquired, take advantage of it to push the current holder
                            if (p_holder.isTreated()) {
                                throw std::runtime_error("Attempt to push a treated holder\n");
                            }

                            task_packet v_buffer = getBuffer(p_holder.getArgs());
                            m_mpi_scheduler->push(std::move(v_buffer)); // this closes the sending channel internally
                            p_holder.setMPISent();
                            m_load_balancer.prune(&p_holder);
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
            int r = try_another_process(holder, f_serial); // TODO .. this method does not exist, maybe remove!
            if (r == 0) {
                return true;
            }
            if (r == 2) {
                return false;
            }

            return push_multithreading<Ret>(f, id, holder);
        }

    public:
        MPI_Comm &get_communicator() {
            return *m_world_communicator;
        }

        // if multiprocessing, BranchHandler should have access to the mpi scheduler
        void pass_mpi_scheduler(scheduler_parent *p_mpi_scheduler) {
            this->m_mpi_scheduler = p_mpi_scheduler;
            this->m_world_rank = this->m_mpi_scheduler->rank_me();
        }

        int get_world_rank() const {
            return m_world_rank;
        }


        //<editor-fold desc="In construction... non-void  functions">
        template<typename Ret, typename HolderType, typename Serialize, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        void reply(Serialize &&p_serialize, HolderType &p_holder, int p_src_rank) {
            utils::print_mpi_debug_comments("rank {} entered reply! \n", m_world_rank);
            // default construction of a return type "Ret"
            Ret res; // TODO .. why in separate lines?
            res = p_holder.get();

            // termination, since all recursions return to center node
            if (p_src_rank == 0) {
                utils::print_mpi_debug_comments("cover size() : {}, sending to center \n", res.coverSize());

                std::string v_buffer;
                p_serialize(v_buffer, res);

                int v_ref_value_local = reference_value();
                task_packet v_candidate{v_buffer};
                m_best_solution_serialized = {score::make(v_ref_value_local), v_candidate};

            } else {
                // some other node requested help, and it is surely waiting for the return value

                utils::print_mpi_debug_comments("rank {} about to reply to {}! \n", m_world_rank, p_src_rank);
                std::unique_lock<std::mutex> lck(m_mpi_mutex); // no other thread can retrieve nor send via MPI

                std::stringstream ss;
                p_serialize(ss, res);
                int count = ss.str().size();
                task_packet v_task_packet(ss.str());

                int err = MPI_Ssend(v_task_packet.data(), count, MPI_BYTE, p_src_rank, 0, *m_world_communicator); // this might be wrong anyway due to the tag
                if (err != MPI_SUCCESS) {
                    spdlog::error("result could not be sent from rank {} to rank {}! \n", m_world_rank, p_src_rank);
                }
            }
        }

        template<typename Ret, typename HolderType, typename Serialize, std::enable_if_t<std::is_void_v<Ret>, int> = 0>
        void reply(Serialize &&, HolderType &, int) {
            m_thread_pool->wait();
        }

        //</editor-fold>


        /**
         *  this method should not be possibly accessed if priority (MPI) not acquired
         */
        template<typename HolderType>
        bool try_top_holder(auto &p_get_buffer, HolderType &p_holder) {
            if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                HolderType *v_upper_holder = m_load_balancer.find_top_holder(&p_holder); //  if it finds it, then the root has already been lowered
                if (v_upper_holder) {
                    if (v_upper_holder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (v_upper_holder->evaluate_branch_checkIn()) {
                        v_upper_holder->setMPISent(true, m_mpi_scheduler->next_process());
                        task_packet v_buffer = p_get_buffer(v_upper_holder->getArgs());
                        m_mpi_scheduler->push(std::move(v_buffer));
                    } else {
                        v_upper_holder->setDiscard();
                        // WARNING, ATTENTION, CUIDADO! holder discarded, flagged as sent but not really sent, then sendingChannel should be released!!!!
                        m_mpi_scheduler->close_sending_channel();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                m_load_balancer.pop_left_sibling(&p_holder); // pops holder from parent's children
            }
            return false; // top holder not found
        }


        /* 	it attempts pushing on another process by default, if none found,
            it attempts to push on another thread, if none found
            it will proceed sequentially
        */
        template<typename Ret, typename F, typename HolderType, typename Serializer>
        bool try_push_mp(F &p_function, int p_id, HolderType &p_holder, Serializer &&p_serializer) {
            bool isSuccess = push_multiprocess(p_id, p_holder, p_serializer);
            return isSuccess ? isSuccess : try_push_mt<Ret>(p_function, p_id, p_holder);
        }

        template<typename Ret, typename F, typename HolderType, typename Deserializer, std::enable_if_t<!std::is_void_v<Ret>, int> = 0>
        Ret forward(F &p_function, int p_thread_id, HolderType &p_holder, Deserializer &p_deserializer, bool) {
            // TODO this is related to non-void function on multiprocessing mode
            // in construction

            if (p_holder.is_pushed() || p_holder.is_MPI_Sent()) {
                // TODO.. this should be considered when using DLB_Handler and pushing to another process
                return p_holder.get(p_deserializer);
                // return {}; // nope, if it was pushed, then the result should be retrieved in here
            }

            if (m_load_balancing_strategy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder);
            }

            return forward<Ret>(p_function, p_thread_id, p_holder);
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
        [[nodiscard]] std::function<std::shared_ptr<ResultHolderParent>(task_packet)> construct_buffer_decoder(auto &p_callable, auto &p_deserializer) {
            using HolderType = ResultHolder<Ret, Args...>;

            utils::print_mpi_debug_comments("About to build Decoder");
            std::function<std::shared_ptr<ResultHolderParent>(task_packet)> decoder = [this, &p_callable, &p_deserializer](task_packet p_packet) {
                std::shared_ptr<ResultHolderParent> smart_ptr = std::make_shared<HolderType>(m_load_balancer, -1);
                auto *holder = dynamic_cast<HolderType *>(smart_ptr.get());

                std::stringstream ss;
                ss.write(reinterpret_cast<const char *>(p_packet.data()), static_cast<int>(p_packet.size()));

                auto _deserializer = std::bind_front(p_deserializer, std::ref(ss));
                std::apply(_deserializer, holder->getArgs());

                force_push<Ret>(p_callable, -1, *holder);

                return smart_ptr;
            };

            utils::print_mpi_debug_comments("Decoder built");

            return decoder;
        }


        // this returns a lambda function which returns the best results as raw data
        [[nodiscard]] std::function<result()> construct_result_fetcher() {
            return [this]() {
                if (m_best_solution_serialized.get_score_as_integer() == -1) {
                    return result::EMPTY;
                } else {
                    return m_best_solution_serialized;
                }
            };
        }

        // meant to be used with non-void functions
        [[maybe_unused]] auto construct_result_fetcher(auto *p_holder, auto &&p_deserializer) {
            return [this]() {
                throw std::runtime_error("Not yet implemented");
            };
        }


        #endif

    private:
        [[nodiscard]] bool should_update_result(const score &p_new_score) const {
            const bool v_new_max_is_better = m_goal == MAXIMISE && p_new_score > m_score;
            const bool v_new_min_is_better = m_goal == MINIMISE && p_new_score < m_score;
            return v_new_max_is_better || v_new_min_is_better;
        }
    };
}

#endif
