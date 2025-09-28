#ifndef BRANCH_HANDLER_H
#define BRANCH_HANDLER_H

#include <any>
#include <atomic>
#include <climits>
#include <config.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <bits/stdc++.h>
#include <dynamic_load_balancer/dynamic_load_balancer_handler.hpp>
#include <load_balancing/api/load_balancer.hpp>
#include <schedulers/api/scheduler.hpp>
#include <spdlog/spdlog.h>
#include <utils/args_handler.hpp>
#include <utils/gempba_utils.hpp>
#include <utils/thread_pool.hpp>
#include <utils/utils.hpp>

/*
 * Created by Andrés Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */
namespace gempba {


    template<typename Ret, typename... Args>
    class result_holder;

    class branch_handler {

        template<typename Ret, typename... Args>
        friend
        class result_holder;

        std::atomic<size_t> m_thread_requests;
        /*This section refers to the strategy wrapping a function
            then pruning data to be used by the wrapped function<<---*/

        std::variant<std::any, task_packet> m_result{};

        dynamic_load_balancer_handler &m_load_balancer = dynamic_load_balancer_handler::getInstance();
        balancing_policy m_balancing_policy = QUASI_HORIZONTAL;

        score m_score = score::make(INT_MIN);
        goal m_goal = MAXIMISE;

        /*------------------------------------------------------>>end*/
        /* "processor_count" would allow setting by default the maximum number of threads
            that the machine can handle unless the user invokes setMaxThreads() */

        unsigned int m_processor_count;
        std::atomic<long long> m_idle_time;
        std::mutex m_mutex; // local mutex
        std::unique_ptr<thread_pool::Pool> m_thread_pool;
        load_balancer *const m_balancer;


        explicit branch_handler(load_balancer *p_load_balancer, scheduler::worker *p_scheduler) :
            m_balancer(p_load_balancer), m_scheduler(p_scheduler) {

            m_processor_count = std::thread::hardware_concurrency();
            m_idle_time = 0;
            m_thread_requests = 0;
            if (m_scheduler) {
                m_world_rank = m_scheduler->rank_me();
                m_world_size = m_scheduler->world_size();
            }
        }

        // Singleton instance
        static std::unique_ptr<branch_handler> m_instance;

    public:
        //<editor-fold desc="Construction/Destruction">
        static branch_handler &create(load_balancer *p_load_balancer, scheduler::worker *p_worker = nullptr) {
            if (!m_instance) {
                if (!p_load_balancer) {
                    // TODO... to be enforced
                }
                m_instance = std::unique_ptr<branch_handler>(new branch_handler(p_load_balancer, p_worker));
            }
            return *m_instance;
        }

        static branch_handler &get_instance() {
            if (!m_instance) {
                spdlog::throw_spdlog_ex("Instance not created yet. Call create() first.");
            }
            return *m_instance;
        }

        /**
         * Resets the singleton instance.
         */
        static void reset_instance() {
            m_instance.reset();
        }

        ~branch_handler() = default;

        branch_handler(const branch_handler &) = delete;

        branch_handler(branch_handler &&) = delete;

        branch_handler &operator=(const branch_handler &) = delete;

        branch_handler &operator=(branch_handler &&) = delete;

        //</editor-fold>

        void set_balancing_policy(const balancing_policy p_strategy) {
            this->m_balancing_policy = p_strategy;
        };

        [[deprecated]] [[nodiscard]] balancing_policy get_balancing_policy() const {
            return m_balancing_policy;
        }

        [[deprecated]][[nodiscard]] double get_pool_idle_time() const {
            return m_thread_pool->idle_time() / static_cast<double>(m_processor_count);
        }

        [[nodiscard]] int get_pool_size() const {
            return static_cast<int>(this->m_thread_pool->size());
        }

        void init_thread_pool(int p_pool_size) {
            this->m_processor_count = p_pool_size;
            m_thread_pool = std::make_unique<thread_pool::Pool>(p_pool_size);
        }

        // seconds
        [[deprecated]] [[nodiscard]] double idle_time() const {
            const double v_nanoseconds = static_cast<double>(m_idle_time) / (static_cast<double>(m_processor_count) + 1.0);
            return v_nanoseconds * 1.0e-9; // convert to seconds
        }

        /**
         * Updates the most up-to-date result. This method attempts to update the result, and should be used only in multithreading mode. (Not multiprocessing)
         * @tparam T Type of the result
         * @param p_new_result the most promising new result for the solution in the scope calling this method
         * @param p_new_score the most promising new score that represents the result
        * @return True if the result was successfully updated, false otherwise.
         */
        template<typename T>
        bool try_update_result(T &p_new_result, const score p_new_score) {
            std::unique_lock v_lock(m_mutex);
            if (!should_update_result(p_new_score)) {
                return false;
            }

            this->m_result = std::make_any<decltype(p_new_result)>(p_new_result);
            this->m_score = p_new_score;
            return true;
        }

        /**
         * This method is thread safe: Updates the most up-to-date result if the new score is better than the current one. This should be used in multiprocessing mode because it
         * uses a serializer to convert the result into bytes.
         *
         * @tparam T Type of the result
         * @param p_new_result the most promising new result for the solution in the scope calling this method
         * @param p_new_score the most promising new score that represents the result
         * @param p_serializer a function that serializes the result into a string
         * @return True if the result was successfully updated, false otherwise.
         */
        template<typename T>
        bool try_update_result(T &p_new_result, score p_new_score, std::function<task_packet(T &)> &p_serializer) {
            std::unique_lock v_lock(m_mutex);

            if (!should_update_result(p_new_score)) {
                return false;
            }

            const auto v_packet = static_cast<task_packet>(p_serializer(p_new_result));
            this->m_result = v_packet;
            this->m_score = p_new_score;

            return true;
        }

        /**
        * Warning: This is not meant to be used directly by the user. This is called only by the scheduler.
        *
        * This method is thread safe: Updates the most up-to-date score if the new score is better than the current one, and clears any previous result.
        *
        * @param p_new_score the most promising new score for the solution in the scope calling this method
        * @return True if the score was successfully updated, false otherwise.
        */
        bool try_update_score_and_invalidate_result(const score &p_new_score) {
            std::scoped_lock<std::mutex> v_lock(m_mutex);

            if (should_update_result(p_new_score)) {
                m_score = p_new_score;
                m_result = task_packet::EMPTY;
                return true;
            }

            return false;
        }

        // get number of successful thread requests
        [[deprecated]] [[nodiscard]] size_t number_thread_requests() const {
            return m_thread_requests.load();
        }

        [[nodiscard]] size_t get_thread_request_count() const {
            return m_balancer->get_thread_request_count();
        }

        void set_goal(const goal p_goal, const score_type p_type) {
            m_goal = p_goal;
            m_score = utils::get_default_score(p_goal, p_type);
        }

        // get number for this rank
        [[nodiscard]] int rank_me() const {
            if (m_scheduler) {
                return m_scheduler->rank_me();
            }
            return -1;
        }

        /**
        * Waits for tasks in the thread pool to complete.
        * Useful for void algorithms, allowing reuse of the pool.
        * Blocks the current thread until all tasks finish.
        * Provides synchronization with the main thread.
        */
        [[deprecated]] void wait() const {
            utils::print_ipc_debug_comments("Main thread waiting results \n");
            this->m_thread_pool->wait();
        }

        // wall time
        static double get_wall_time() {
            return utils::wall_time();
        }

        [[deprecated]] [[nodiscard]] bool has_result() const {
            throw std::logic_error("Not implemented yet!");
        }

        [[deprecated]] bool is_done() const {
            return m_thread_pool->hasFinished();
        }


        /**
         * if running in multithreading mode, the best solution can directly fetch without any deserialization
         * @tparam Ret
         */
        template<typename Ret>
        [[nodiscard]] auto fetch_result() -> Ret {
            std::unique_lock v_lock(m_mutex);
            if (!std::holds_alternative<std::any>(m_result)) {
                spdlog::throw_spdlog_ex(std::format("Attempt to fetch a result of type {} but the result is not set", typeid(Ret).name()));
            }
            if (auto v_any = std::get<std::any>(m_result); v_any.has_value()) {
                return std::any_cast<Ret>(v_any);
            }
            spdlog::throw_spdlog_ex(std::format("Attempt to fetch a result of type {} but the result is not set", typeid(Ret).name()));
        }

        [[nodiscard]] score get_score() const {
            return m_score;
        }

        std::optional<result> get_result_bytes() {
            std::unique_lock v_lock(m_mutex);
            if (!std::holds_alternative<task_packet>(m_result)) {
                return std::nullopt;
            }
            return result(m_score, std::get<task_packet>(m_result));
        }

        //</editor-fold>

        template<typename T>
        std::optional<T> get_result() {
            std::unique_lock v_lock(m_mutex);
            if (!std::holds_alternative<std::any>(m_result)) {
                spdlog::error("Attempt to get a result of type {} but the result is not set", typeid(T).name());
                return std::nullopt;
            }

            if (auto v_any = std::get<std::any>(m_result); v_any.has_value()) {
                return std::any_cast<T>(v_any);
            }
            return std::nullopt;
        }

        /**
         * if multiprocessing is used, then every process should call this method before starting
         * @param p_score first approximation of the score that represents the best solution
         */
        void set_score(const score &p_score) {
            this->m_score = p_score;
        }

        /**
         * Asynchronous operation:
         * Special care should be taken with this method, otherwise deadlocks might occur.
         * It could be used once for pushing the first time.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType>
        [[deprecated]] void force_local_submit(F &p_function, int p_thread_id, HolderType &p_holder) requires (!std::is_void_v<Ret>) {
            p_holder.setPushStatus();
            m_load_balancer.prune(&p_holder);
            args_handler::unpack_and_forward_non_void(p_function, p_thread_id, p_holder.getArgs(), p_holder);
        }

        /**
         * @brief Synchronous operation:
         *
         * It forces the function to be executed in the current thread.
         *
         * @tparam Ret return type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType>
        [[deprecated]] void forward(F &p_function, int p_thread_id, HolderType &p_holder) {
            forward_helper<Ret>(p_function, p_thread_id, p_holder);
        }

        /**
         * Asynchronous operation:
         * It could be used once when submitting the first task.
         * @tparam Ret return type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType>
        [[deprecated]] void force_local_submit(F &p_function, [[maybe_unused]] int p_thread_id, HolderType &p_holder) requires (std::is_void_v<Ret>) {
            p_holder.setPushStatus();
            m_load_balancer.prune(&p_holder);
            args_handler::unpack_and_push_void(*m_thread_pool, p_function, p_holder.getArgs());
        }

        /**
         *
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         * @return
         */
        template<typename Ret, typename F, typename HolderType>
        [[deprecated]] bool try_local_submit(F &&p_function, int p_thread_id, HolderType &p_holder) {
            if (send<Ret>(p_function, p_thread_id, p_holder)) {
                return true;
            }
            forward<Ret>(p_function, p_thread_id, p_holder);
            return false;
        }

        #if GEMPBA_MULTIPROCESSING
        /**
         * It attempts to submit a task to the remote rank. If the remote rank is not available, it falls back to try_local_submit
         * @tparam Ret return type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         * @param p_serializer function that serializes the arguments into a gempba::task_packet
         * @return
         */
        template<typename Ret, typename F, typename HolderType, typename Serializer>
        [[deprecated]] bool try_remote_submit(F &p_function, int p_thread_id, HolderType &p_holder, Serializer &&p_serializer) {
            if (send(p_thread_id, p_holder, p_serializer)) {
                return true;
            }
            return try_local_submit<Ret>(p_function, p_thread_id, p_holder);
        }
        #endif

        void set_thread_pool_size(const unsigned int p_size) const {
            m_balancer->set_thread_pool_size(p_size);
        }

        [[nodiscard]] balancing_policy get_balancing_policy2() const {
            return m_balancer->get_balancing_policy();
        }

        [[nodiscard]] unsigned int generate_unique_id() const {
            return m_balancer->generate_unique_id();
        }

        std::future<std::any> force_local_submit(std::function<std::any()> &&p_function) const {
            return m_balancer->force_local_submit(std::move(p_function));
        }

        void forward(node &p_node) const {
            m_balancer->forward(p_node);
        }

        bool try_local_submit(node &p_node) const {
            return m_balancer->try_local_submit(p_node);
        }

        bool try_remote_submit(node &p_node, const int p_runnable_id) const {
            return m_balancer->try_remote_submit(p_node, p_runnable_id);
        }

        [[nodiscard]] double get_idle_time() const {
            return m_balancer->get_idle_time();
        }

        void wait2() const {
            // TODO ... to be renamed
            m_balancer->wait();
        }

        [[nodiscard]] bool is_done2() const {
            // TODO ... to be renamed
            return m_balancer->is_done();
        }

    private:
        /**
         * Helper method to handle forwarding based on non-void return types.
         * @tparam Ret return type
         * @tparam F function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments and potential result
         */
        template<typename Ret, typename F, typename HolderType>
        void forward_helper(F &p_function, int p_thread_id, HolderType &p_holder) requires (!std::is_void_v<Ret>) {
            auto v_ret = forward_internal<Ret>(p_function, p_thread_id, p_holder, true);
            p_holder.hold_actual_result(v_ret);
        }

        /**
         * Helper method to handle forwarding based on void return types.
         * @tparam Ret return type
         * @tparam F  function type
         * @tparam HolderType ResultHolder type
         * @param p_function function to be executed
         * @param p_thread_id thread identifier
         * @param p_holder ResultHolder instance that wraps the function arguments
         */
        template<typename Ret, typename F, typename HolderType>
        void forward_helper(F &p_function, int p_thread_id, HolderType &p_holder) requires (std::is_void_v<Ret>) {
            forward_internal<Ret>(p_function, p_thread_id, p_holder);
        }

        // no DLB_Handler begin **********************************************************************

        template<typename Ret, typename F, typename HolderType>
        Ret forward_internal(F &p_function, int p_thread_id, HolderType &p_holder) requires (!std::is_void_v<Ret>) {
            // TODO this is related to non-void function on multithreading mode
            // DLB not supported
            p_holder.setForwardStatus();
            return args_handler::unpack_and_forward_non_void(p_function, p_thread_id, p_holder.getArgs(), &p_holder);
        }

        // no DLB_Handler ************************************************************************* end

        template<typename Ret, typename F, typename HolderType>
        void forward_internal(F &p_function, int p_thread_id, HolderType &p_holder) requires (std::is_void_v<Ret>) {
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
            if (m_balancing_policy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder); // it checks if root must be moved
            }

            p_holder.setForwardStatus();
            args_handler::unpack_and_forward_void(p_function, p_thread_id, p_holder.getArgs(), &p_holder);
        }

        template<typename Ret, typename F, typename HolderType>
        Ret forward_internal(F &p_function, int p_thread_id, HolderType &p_holder, bool) requires (!std::is_void_v<Ret>) {
            // TODO this is related to non-void function on multithreading mode
            // in construction, DLB may be supported
            if (p_holder.is_pushed()) {
                return p_holder.get();
            }

            if (m_balancing_policy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder);
            }

            return forward_internal<Ret>(p_function, p_thread_id, p_holder);
        }

        #if GEMPBA_MULTIPROCESSING
        template<typename Ret, typename F, typename HolderType, typename Deserializer>
        Ret forward_internal(F &p_function, int p_thread_id, HolderType &p_holder, Deserializer &p_deserializer, bool) requires (!std::is_void_v<Ret>) {
            // TODO this is related to non-void function on multiprocessing mode
            // in construction

            if (p_holder.is_pushed() || p_holder.is_MPI_Sent()) {
                // TODO.. this should be considered when using DLB_Handler and pushing to another process
                return p_holder.get(p_deserializer);
                // return {}; // nope, if it was pushed, then the result should be retrieved in here
            }

            if (m_balancing_policy == QUASI_HORIZONTAL) {
                m_load_balancer.checkLeftSibling(&p_holder);
            }

            return forward_internal<Ret>(p_function, p_thread_id, p_holder);
        }
        #endif

        /**
      * this method should not possibly be accessed if priority (Thread Pool) is not acquired
      */
        template<typename Ret, typename F, typename HolderType>
        bool try_push_root_level_holder_remotely(F &p_function, HolderType &p_holder) requires (std::is_void_v<Ret>) {
            if (m_balancing_policy == QUASI_HORIZONTAL) {
                HolderType *v_upper_holder = m_load_balancer.find_top_holder(&p_holder);
                if (v_upper_holder) {
                    if (v_upper_holder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (v_upper_holder->evaluate_branch_checkIn()) {
                        // checks if it's worth it to push
                        ++this->m_thread_requests;
                        v_upper_holder->setPushStatus();
                        args_handler::unpack_and_push_void(*m_thread_pool, p_function, v_upper_holder->getArgs());
                    } else {
                        // discard otherwise
                        v_upper_holder->setDiscard();
                    }
                    return true; // top holder found whether discarded or pushed
                }
                m_load_balancer.pop_left_sibling(&p_holder); // pops holder from parent's children
            }
            return false; // top holder isn't found or just DLB is disabled
        }

        #if GEMPBA_MULTIPROCESSING
        /**
         * @brief this method should not be possibly accessed if priority (IPC) not acquired
         * @return true if top holder found and pushed, false otherwise
         */
        template<typename HolderType>
        bool try_push_root_level_holder_remotely(auto &p_get_buffer, HolderType &p_holder) {
            if (m_balancing_policy == QUASI_HORIZONTAL) {
                HolderType *v_upper_holder = m_load_balancer.find_top_holder(&p_holder); //  if it finds it, then the root has already been lowered
                if (v_upper_holder) {
                    if (v_upper_holder->isTreated())
                        throw std::runtime_error("Attempt to push a treated holder\n");

                    if (v_upper_holder->evaluate_branch_checkIn()) {
                        v_upper_holder->setMPISent(true, m_scheduler->next_process());
                        task_packet v_buffer = p_get_buffer(v_upper_holder->getArgs());
                        m_scheduler->push(std::move(v_buffer));
                    } else {
                        v_upper_holder->setDiscard();
                        // WARNING, ATTENTION, CUIDADO! holder discarded, flagged as sent but not really sent, then sendingChannel should be released!!!!
                    }
                    return true; // top holder found whether discarded or pushed
                }
                m_load_balancer.pop_left_sibling(&p_holder); // pops holder from parent's children
            }
            return false; // top holder not found
        }
        #endif


        //——————— IPC related attributes and member functions ———————//
    private:
        scheduler::worker *m_scheduler;
        std::mutex m_ipc_mutex; // mutex to ensure funnel access to IPC
        int m_world_rank = -1; // get the rank of the process
        int m_world_size = -1; // get the number of processes/nodes
        char m_processor_name[128]{}; // name of the node

        bool send([[maybe_unused]] int p_thread_id, auto &p_holder, auto &&p_serializer) {
            /* the underlying loop breaks under one of the following scenarios:
                - unable to acquire priority
                - unable to acquire mutex
                - there is not next available process
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock v_lock(m_ipc_mutex, std::defer_lock);
                if (!v_lock.try_lock()) {
                    return false;
                }
                const std::optional<transmission_guard> v_opt = m_scheduler->try_open_transmission_channel();
                // if mutex acquired, other threads will jump this section
                if (!v_opt) {
                    return false;
                }

                auto v_buffer_getter = [&p_serializer](auto &p_tuple) {
                    return std::apply(p_serializer, p_tuple);
                };

                const bool v_top_holder_found_and_pushed = try_push_root_level_holder_remotely(v_buffer_getter, p_holder);
                if (v_top_holder_found_and_pushed) {
                    // if top holder found, then it is pushed; therefore, priority is release internally
                    continue; // keeps iterating from root to current level
                }

                // since priority is already acquired, take advantage of it to push the current holder
                if (p_holder.isTreated()) {
                    throw std::runtime_error("Attempt to push a treated holder\n");
                }

                task_packet v_buffer = v_buffer_getter(p_holder.getArgs());
                m_scheduler->push(std::move(v_buffer)); // this closes the sending channel internally
                p_holder.setMPISent();
                m_load_balancer.prune(&p_holder);
                return true;
            }
        }

    public:
        /*
            Types must be passed through the brackets construct_buffer_decoder<Ret, Args...>(..), so it is
            known at compile time.

            Ret: stands for the return type of the main function
            Args...: is the type list of the original type of the function, without considering int id, and void* parent

            input: this method receives the main algorithm and a deserializer.

            return:  a lambda object that is in charge of receiving a raw buffer in scheduler::run_node(...), this
            lambda object will deserialize the buffer and create a new Holder containing
            the deserialized arguments.
            Lambda object will push to the thread pool, and it will return a pointer to the holder
            */
        template<typename Ret, typename... Args>
        [[nodiscard]] std::function<std::shared_ptr<result_holder_parent>(task_packet)> construct_buffer_decoder(auto &p_callable, auto &p_deserializer) {
            using HolderType = result_holder<Ret, Args...>;

            utils::print_ipc_debug_comments("About to build Decoder");
            std::function<std::shared_ptr<result_holder_parent>(task_packet)> v_decoder = [this, &p_callable, &p_deserializer](task_packet p_packet) {
                std::shared_ptr<result_holder_parent> v_smart_ptr = std::make_shared<HolderType>(m_load_balancer, -1);
                auto *v_holder = dynamic_cast<HolderType *>(v_smart_ptr.get());

                std::stringstream v_ss;
                v_ss.write(reinterpret_cast<const char *>(p_packet.data()), static_cast<int>(p_packet.size()));

                auto v_deserializer = std::bind_front(p_deserializer, std::ref(v_ss));
                std::apply(v_deserializer, v_holder->getArgs());

                force_local_submit<Ret>(p_callable, -1, *v_holder);

                return v_smart_ptr;
            };

            utils::print_ipc_debug_comments("Decoder built");

            return v_decoder;
        }

        // this returns a lambda function which returns the best results as raw data
        [[nodiscard]] std::function<result()> construct_result_fetcher() {
            return [this]() {
                const std::optional<result> v_bytes = get_result_bytes();
                if (v_bytes.has_value()) {
                    return v_bytes.value();
                }
                return result::EMPTY;
            };
        }

    private:
        template<typename Ret, typename F, typename HolderType>
        bool send(F &&p_function, [[maybe_unused]] int p_thread_id, HolderType &p_holder) requires (std::is_void_v<Ret>) {
            /* the underlying loop breaks under one of the following scenarios:
                - mutex cannot be acquired
                - there is no available thread in the pool
                - current level holder is pushed

                NOTE: if top holder found, it'll keep trying to find more
            */
            while (true) {
                std::unique_lock v_lock(m_mutex, std::defer_lock);
                if (!v_lock.try_lock()) {
                    break;
                }
                if (m_thread_pool->n_idle() <= 0) {
                    break; // mutex released at destruction
                }
                const bool v_top_holder_found_and_pushed = try_push_root_level_holder_remotely<Ret>(p_function, p_holder);
                if (v_top_holder_found_and_pushed) {
                    continue; // keeps iterating from root to current level
                }
                if (p_holder.isTreated()) {
                    throw std::runtime_error("Attempt to push a treated holder\n");
                }

                // after this line, only leftMost holder should be pushed
                ++this->m_thread_requests;
                p_holder.setPushStatus();
                m_load_balancer.prune(&p_holder);

                args_handler::unpack_and_push_void(*m_thread_pool, p_function, p_holder.getArgs());
                return true; // pushed to the pool
            }
            return false;
        }

        template<typename Ret, typename F, typename HolderType>
        bool send(F &p_function, [[maybe_unused]] int p_thread_id, HolderType &p_holder) requires (!std::is_void_v<Ret>) {
            /*This lock must be acquired before checking the condition,
            even though numThread is atomic*/
            std::unique_lock v_lock(m_mutex);
            // if (busyThreads < thread_pool->size())
            if (m_thread_pool->n_idle() <= 0) {
                return false;
            }

            if (m_balancing_policy == QUASI_HORIZONTAL) {
                // bool res = try_top_holder<Ret>(lck, f, holder);
                // if (res)
                //	return false; //if top holder found, then it should return false to keep trying

                m_load_balancer.pop_left_sibling(&p_holder);
            }
            ++this->m_thread_requests;
            p_holder.setPushStatus();

            v_lock.unlock();
            auto v_ret = args_handler::unpack_and_push_non_void(*m_thread_pool, p_function, p_holder.getArgs());
            p_holder.hold_future(std::move(v_ret));
            return true;
        }

        [[nodiscard]] bool should_update_result(const score &p_new_score) const {
            const bool v_new_max_is_better = m_goal == MAXIMISE && p_new_score > m_score;
            const bool v_new_min_is_better = m_goal == MINIMISE && p_new_score < m_score;
            return v_new_max_is_better || v_new_min_is_better;
        }
    };
}

#endif
