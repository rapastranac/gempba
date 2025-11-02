/*
* MIT License
 *
 * Copyright (c) 2025. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef GEMPBA_WORK_STEALING_LOAD_BALANCER_HPP
#define GEMPBA_WORK_STEALING_LOAD_BALANCER_HPP

#include <BS_thread_pool.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/utils.hpp>
#include <gempba/core/load_balancer.hpp>
#include <gempba/core/node.hpp>
#include <spdlog/spdlog.h>

/**
 * @author Andres Pastrana
 * @date 2025-08-30
 */

namespace gempba {
    class work_stealing_load_balancer final : public load_balancer {

        BS::thread_pool<> m_thread_pool;
        unsigned int m_unique_id_counter = 0;
        std::recursive_mutex m_recursive_mutex;
        std::size_t m_thread_request_count = 0;

        scheduler::worker *const m_scheduler_worker;

    public:
        explicit work_stealing_load_balancer(scheduler::worker *const p_scheduler_worker = nullptr) :
            m_scheduler_worker(p_scheduler_worker) {
        }

        balancing_policy get_balancing_policy() override {
            return WORK_STEALING;
        }

        unsigned generate_unique_id() override {
            return ++m_unique_id_counter;
        }

        std::future<std::any> force_local_submit(std::function<std::any()> &&p_function) override {
            ++m_thread_request_count;
            return m_thread_pool.submit_task(p_function);
        }

        void forward(node &p_node) override {
            if (p_node.get_state() == UNUSED && p_node.should_branch()) {
                p_node.run();
            }
            p_node.prune();
        }

        bool try_local_submit(node &p_node) override {
            if (send(p_node)) {
                return true;
            }
            forward(p_node);
            return false;
        }

        bool try_remote_submit(node &p_node, const int p_runnable_id) override {
            if (!m_scheduler_worker) {
                utils::log_and_throw("Attempted to do remote submission without a scheduler worker");
            }
            if (send(p_node, p_runnable_id)) {
                return true;
            }
            try_local_submit(p_node);
            return false;
        }

        [[nodiscard]] double get_idle_time() const override {
            return m_thread_pool.get_wall_idle_time();
        }

        std::shared_ptr<std::shared_ptr<node_core> > get_root(std::thread::id) override {
            // No-op for work stealing load balancer
            return {};
        }

        void set_root(std::thread::id, std::shared_ptr<node_core> &) override {
            // No-op for work stealing load balancer
        }

        void set_thread_pool_size(const unsigned p_size) override {
            m_thread_pool.reset(p_size);
            spdlog::debug("thread pool size: {}",  m_thread_pool.get_thread_count());
        }

        void wait() override {
            m_thread_pool.wait();
        }

        bool is_done() const override {
            return m_thread_pool.get_tasks_total() == 0;
        }

        std::size_t get_thread_request_count() const override {
            return m_thread_request_count;
        }

    private:
        /**
        * @brief Utility method to check if the thread_pool can receive another task. Always use within a lock_guard as
        * it might change when attempting to submit a new task.
        *
        * @return Returns true if the thread pool is not full, false otherwise.
        */
        bool is_thread_pool_full() const {
            const unsigned int v_thread_count = m_thread_pool.get_thread_count();
            const unsigned int v_tasks_running = m_thread_pool.get_tasks_running();
            return v_tasks_running == v_thread_count;
        }


        bool send(node &p_node) {
            const std::unique_lock v_lock(m_recursive_mutex, std::try_to_lock);
            if (!v_lock.owns_lock()) {
                return false;
            }
            if (is_thread_pool_full()) {
                return false;
            }
            // push current node
            if (p_node.is_consumed()) {
                utils::log_and_throw("Node: {} is already consumed", p_node.get_node_id());
            }

            // —————— after this line, only leftMost node should be pushed ——————————————————————————————————————————————
            // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
            p_node.prune();
            if (p_node.should_branch()) {
                p_node.delegate_locally(this);
            } else {
                p_node.set_state(DISCARDED);
            }
            return true; // signalize that node was consumed
        }

        bool send(node &p_node, const int p_runnable_id) {
            const std::unique_lock v_lock(m_recursive_mutex, std::try_to_lock);
            if (!v_lock.owns_lock()) {
                return false;
            }

            const std::optional<transmission_guard> v_guard_optional = m_scheduler_worker->try_open_transmission_channel();
            if (!v_guard_optional.has_value()) {
                return false;
            }
            // push current node
            if (p_node.is_consumed()) {
                utils::log_and_throw("Node: {} is already consumed", p_node.get_node_id());
            }

            // node should be pruned before submission —— ORDER MATTERS (avoids memory leaks)
            p_node.prune();
            if (p_node.should_branch()) {
                p_node.delegate_remotely(m_scheduler_worker, p_runnable_id);
            } else {
                p_node.set_state(DISCARDED);
            }
            return true; // signalize that node was consumed
        }


        void prune(const std::shared_ptr<node_core> &p_core) {
            p_core->prune();
            // p_node->set_root(nullptr);
            // p_node->set_parent(nullptr);
        }

    };
}


#endif //GEMPBA_WORK_STEALING_LOAD_BALANCER_HPP
