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
#ifndef GEMPBA_NODE_HPP
#define GEMPBA_NODE_HPP

#include <any>
#include <functional>
#include <iostream>
#include <list>
#include <memory>
#include <thread>
#if __has_include(<stacktrace>)
    #include <stacktrace>
    #define GEMPBA_HAS_STACKTRACE 1
#else
    #define GEMPBA_HAS_STACKTRACE 0
#endif

#include <gempba/core/node_core.hpp>
#include <gempba/core/node_traits.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/utils/utils.hpp>


/**
 * @author Andres Pastrana
 * @date 2024-08-25
 */
namespace gempba {

    /**
     * @brief Interface for a node in the trace tree. This node represents the top of a function call. It can be a dummy
     * node, which means that it doesn't wrap any arguments, or a real node, which means that it wraps arguments.
     *
     * A dummy node is a node that is created by the system to represent a function call that is not part of the trace tree, as a wau to
     * link the trace tree to the system.
     */
    class node final : public node_traits<node> {
        std::shared_ptr<node_core> m_node_core;

        static void throw_if_null(const std::shared_ptr<node_core> &p_node) {
            if (!p_node) {
                std::stringstream v_ss;
#if GEMPBA_HAS_STACKTRACE
                v_ss << std::stacktrace::current() << std::endl;
#else
                v_ss << "[stacktrace unavailable: std::stacktrace not implemented by this standard library]" << std::endl;
#endif
                std::cerr << v_ss.str();
                std::cerr.flush(); // Extra flush for paranoia

                utils::log_and_throw("Attempted to access a null node");
            }
        }

    public:
        static node create(const node &p_parent, const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> &p_factory) { return node(p_factory(p_parent.m_node_core)); }

        static node create(const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> &p_factory) { return node(p_factory(nullptr)); }

        node() = default;

        ~node() override = default;

        explicit node(const std::shared_ptr<node_core> &p_other) : m_node_core(p_other) {}

        // Copy Constructor
        node(const node &p_other) = default;

        // Copy Assignment
        node &operator=(const node &p_other) = default;

        // Move Constructor
        node(node &&p_other) noexcept = default;

        // Move Assignment
        node &operator=(node &&p_other) noexcept = default;

        // Operators
        friend bool operator==(const node &p_lhs, const node &p_rhs) { return p_lhs.m_node_core == p_rhs.m_node_core; }

        explicit operator bool() const { return m_node_core != nullptr; }

        bool operator==(std::nullptr_t) const noexcept { return m_node_core == nullptr; }

        bool operator!=(std::nullptr_t) const noexcept { return m_node_core != nullptr; }

        node map_core(std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core> &)> &&p_accessor) { return node(p_accessor(m_node_core)); }

        void apply_core(std::function<void(std::shared_ptr<node_core> &)> &&p_accessor) { p_accessor(m_node_core); }

        void run() override {
            throw_if_null(m_node_core);
            m_node_core->run();
        }

        void delegate_locally(load_balancer *p_load_balancer) override {
            throw_if_null(m_node_core);
            m_node_core->delegate_locally(p_load_balancer);
        }

        void delegate_remotely(scheduler::worker *p_worker, const int p_runner_id) override {
            throw_if_null(m_node_core);
            m_node_core->delegate_remotely(p_worker, p_runner_id);
        }

        void set_result(const task_packet &p_result) override {
            throw_if_null(m_node_core);
            m_node_core->set_result(p_result);
        }

        [[nodiscard]] task_packet get_result() override {
            throw_if_null(m_node_core);
            return m_node_core->get_result();
        }

        task_packet serialize() override {
            throw_if_null(m_node_core);
            return m_node_core->serialize();
        }

        void deserialize(const task_packet &p_buffer) override {
            throw_if_null(m_node_core);
            m_node_core->deserialize(p_buffer);
        }

        void set_result_serializer(const std::function<task_packet(std::any)> &p_result_serializer) override {
            throw_if_null(m_node_core);
            m_node_core->set_result_serializer(p_result_serializer);
        }

        void set_result_deserializer(const std::function<std::any(task_packet)> &p_result_deserializer) override {
            throw_if_null(m_node_core);
            m_node_core->set_result_deserializer(p_result_deserializer);
        }

        [[nodiscard]] bool is_dummy() const override {
            throw_if_null(m_node_core);
            return m_node_core->is_dummy();
        }

        [[nodiscard]] std::thread::id get_thread_id() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_thread_id();
        }

        [[nodiscard]] int get_node_id() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_node_id();
        }

        [[nodiscard]] int get_forward_count() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_forward_count();
        }

        [[nodiscard]] int get_push_count() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_push_count();
        }

        [[nodiscard]] node_state get_state() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_state();
        }

        void set_state(const node_state p_state) override {
            throw_if_null(m_node_core);
            m_node_core->set_state(p_state);
        }

        [[nodiscard]] bool is_consumed() const override {
            throw_if_null(m_node_core);
            return m_node_core->is_consumed();
        }

        [[nodiscard]] node get_root() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_root());
        }

        void set_parent(const node &p_parent) override {
            throw_if_null(m_node_core);
            m_node_core->set_parent(p_parent.m_node_core);
        }

        [[nodiscard]] node get_parent() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_parent());
        }

        [[nodiscard]] node get_leftmost_child() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_leftmost_child());
        }

        [[nodiscard]] node get_second_leftmost_child() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_second_leftmost_child());
        }

        void remove_leftmost_child() override {
            throw_if_null(m_node_core);
            m_node_core->remove_leftmost_child();
        }

        void remove_second_leftmost_child() override {
            throw_if_null(m_node_core);
            m_node_core->remove_second_leftmost_child();
        }

        [[nodiscard]] node get_leftmost_sibling() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_leftmost_sibling());
        }

        [[nodiscard]] node get_left_sibling() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_left_sibling());
        }

        [[nodiscard]] node get_right_sibling() override {
            throw_if_null(m_node_core);
            return node(m_node_core->get_right_sibling());
        }

        [[deprecated]] std::list<node> get_children() override {
            throw_if_null(m_node_core);
            // TODO ... this could be removed
            std::list<node> v_temp;
            // Not optimal
            for (const auto &v_core: m_node_core->get_children()) {
                v_temp.emplace_back(v_core);
            }
            return v_temp;
        }

        [[nodiscard]] int get_children_count() const override {
            throw_if_null(m_node_core);
            return m_node_core->get_children_count();
        }

        void add_child(const node &p_child) override {
            throw_if_null(m_node_core);
            m_node_core->add_child(p_child.m_node_core);
        }

        std::any get_any_result() override {
            throw_if_null(m_node_core);
            return m_node_core->get_any_result();
        }

        [[nodiscard]] bool is_result_ready() const override {
            throw_if_null(m_node_core);
            return m_node_core->is_result_ready();
        }

        bool should_branch() override {
            throw_if_null(m_node_core);
            return m_node_core->should_branch();
        }

        void remove_child(node &p_node) override { m_node_core->remove_child(p_node.m_node_core); }

        void prune() override { m_node_core->prune(); }
    };

    // Non-member operator== for nullptr on the left-hand side
    inline bool operator==(std::nullptr_t, const node &p_n) noexcept {
        return p_n == nullptr; // Reuse the member function
    }

    // Non-member operator!= for nullptr on the left-hand side
    inline bool operator!=(std::nullptr_t, const node &p_n) noexcept {
        return p_n != nullptr; // Reuse the member function
    }

    template<typename F, typename Ret, typename... Args>
    concept invokable = std::invocable<F, std::thread::id, Args..., node> && std::same_as<std::invoke_result_t<F, std::thread::id, Args..., node>, Ret>;
} // namespace gempba

#endif // GEMPBA_NODE_HPP
