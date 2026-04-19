#ifndef BRANCH_HANDLER_H
#define BRANCH_HANDLER_H

#include <any>
#include <atomic>
#include <climits>
#include <gempba/config.h>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <tuple>
#include <utility>
#include <variant>
#include <spdlog/spdlog.h>

#include <gempba/core/load_balancer.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/utils/gempba_utils.hpp>
#include <gempba/utils/utils.hpp>

/*
 * Created by Andrés Pastrana on 2019
 * pasr1602@usherbrooke.ca
 * rapastranac@gmail.com
 */
namespace gempba {
    class node_manager {

        std::atomic<size_t> m_thread_requests;
        /*This section refers to the strategy wrapping a function
            then pruning data to be used by the wrapped function<<---*/

        std::variant<std::any, task_packet> m_result{};

        balancing_policy m_balancing_policy = QUASI_HORIZONTAL;

        score m_score = score::make(INT_MIN);
        goal m_goal = MAXIMISE;

        /*------------------------------------------------------>>end*/
        /* "processor_count" would allow setting by default the maximum number of threads
            that the machine can handle unless the user invokes setMaxThreads() */

        unsigned int m_processor_count;
        std::atomic<long long> m_idle_time;
        std::mutex m_mutex; // local mutex
        load_balancer *const m_balancer;

    public:
        //<editor-fold desc="Construction/Destruction">

        explicit node_manager(load_balancer *const p_load_balancer, scheduler::worker *p_scheduler) :
            m_balancer(p_load_balancer), m_scheduler(p_scheduler) {

            m_processor_count = std::thread::hardware_concurrency();
            m_idle_time = 0;
            m_thread_requests = 0;
            if (m_scheduler) {
                m_world_rank = m_scheduler->rank_me();
                m_world_size = m_scheduler->world_size();
            }
        }

        ~node_manager() = default;

        node_manager(const node_manager &) = delete;

        node_manager(node_manager &&) = delete;

        node_manager &operator=(const node_manager &) = delete;

        node_manager &operator=(node_manager &&) = delete;

        //</editor-fold>

        void set_balancing_policy(const balancing_policy p_strategy) {
            this->m_balancing_policy = p_strategy;
        };

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
                spdlog::debug("rank {}, invalidated existing score : {} -> new score: {}", rank_me(), m_score.to_string(), p_new_score.to_string());
                m_score = p_new_score;
                m_result = task_packet::EMPTY;
                return true;
            }

            return false;
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

        // wall time
        static double get_wall_time() {
            return utils::wall_time();
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

        void set_thread_pool_size(const unsigned int p_size) const {
            m_balancer->set_thread_pool_size(p_size);
        }

        [[nodiscard]] balancing_policy get_balancing_policy() const {
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

        void wait() const {
            m_balancer->wait();
        }

        [[nodiscard]] bool is_done() const {
            return m_balancer->is_done();
        }

        //——————— IPC related attributes and member functions ———————//
    private:
        scheduler::worker *m_scheduler;
        std::mutex m_ipc_mutex; // mutex to ensure funnel access to IPC
        int m_world_rank = -1; // get the rank of the process
        int m_world_size = -1; // get the number of processes/nodes
        char m_processor_name[128]{}; // name of the node


        [[nodiscard]] bool should_update_result(const score &p_new_score) const {
            const bool v_new_max_is_better = m_goal == MAXIMISE && p_new_score > m_score;
            const bool v_new_min_is_better = m_goal == MINIMISE && p_new_score < m_score;
            return v_new_max_is_better || v_new_min_is_better;
        }
    };
}

#endif
