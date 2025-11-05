/*
 * MIT License
 *
 * Copyright (c) 2024. Andrés Pastrana
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

#ifndef GEMPBA_NODE_IMPL_HPP
#define GEMPBA_NODE_IMPL_HPP

#include <any>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

#include <gempba/core/load_balancer.hpp>
#include <gempba/core/node_core.hpp>
#include <gempba/utils/utils.hpp>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    template<typename T>
    class node_core_impl;

    template<typename Ret, typename... Args>
    class node_core_impl<Ret(Args...)> final : public node_core, public std::enable_shared_from_this<node_core> {
        enum initialization_type {
            UNINITIALIZED,
            INITIALIZED,
            LAZY,
            DESERIALIZED,
        };


        std::shared_ptr<std::shared_ptr<node_core> > m_root;
        std::shared_ptr<node_core> m_parent;
        std::list<std::shared_ptr<node_core> > m_children;

        std::variant<std::any, std::future<std::any> > m_result; // Result of the function call (if any)

        std::function<Ret(std::thread::id, Args..., node)> m_runnable;
        std::function<std::optional<std::tuple<Args...> >()> m_initializer;

        std::function<task_packet(Args...)> m_args_serializer;
        std::function<std::tuple<Args...>(task_packet)> m_args_deserializer;

        std::function<task_packet(std::any)> m_result_serializer;
        std::function<std::any(task_packet)> m_result_deserializer;
        std::optional<bool> m_should_branch_cached = std::nullopt;

        int m_initialization_flag = UNINITIALIZED;

        load_balancer &m_load_balancer;

        std::tuple<Args...> m_arguments;
        node_state m_state = UNUSED;
        int m_forward_count = 0;
        int m_push_count = 0;

        int m_node_id = -1;
        std::thread::id m_thread_id{};
        bool m_is_dummy = false;

        //<editor-fold desc="Multiprocessing member variables">
        unsigned int m_remote_node = -1; // rank destination
        //</editor-fold>


        template<typename T = Ret>
        std::any m_invoke(const bool p_delegated) requires(std::is_void_v<T>) {
            auto v_parent_node = p_delegated ? node() : node(shared_from_this());
            std::thread::id v_thread_id = std::this_thread::get_id();
            auto v_args = std::tuple_cat(std::make_tuple(v_thread_id), m_arguments, std::make_tuple(v_parent_node));
            std::apply(m_runnable, v_args);
            return {};
        }

        template<typename T = Ret>
        std::any m_invoke(const bool p_delegated) requires(!std::is_void_v<T>) {
            auto v_parent_node = p_delegated ? node() : node(shared_from_this());
            std::thread::id v_thread_id = std::this_thread::get_id();
            auto v_args = std::tuple_cat(std::make_tuple(v_thread_id), m_arguments, std::make_tuple(v_parent_node));
            return std::make_any<T>(std::apply(m_runnable, v_args));
        }

        // Explicitly initialized with serializer/deserializer
        explicit node_core_impl(load_balancer &p_load_balancer, invokable<Ret, Args...> auto &&p_runnable, std::tuple<Args...> &&p_args,
                                std::function<task_packet(Args...)> p_args_serializer, std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) :
            node_core_impl(p_load_balancer, false) {

            m_runnable = std::forward<decltype(p_runnable)>(p_runnable);

            m_initialization_flag = INITIALIZED;
            m_args_serializer = p_args_serializer;
            m_args_deserializer = p_args_deserializer;
            m_arguments = std::move(p_args);
            m_initializer = [&] {
                return m_arguments;
            };
        }

        // Lazily initialized with serializer/deserializer
        explicit node_core_impl(load_balancer &p_load_balancer, invokable<Ret, Args...> auto &&p_runnable, std::function<std::optional<std::tuple<Args...> >()> p_initializer,
                                std::function<task_packet(Args...)> p_args_serializer, std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) :
            node_core_impl(p_load_balancer, p_runnable, p_args_serializer, p_args_deserializer) {

            m_initializer = p_initializer;
            m_initialization_flag = LAZY;
        }

        // Placeholder node that can be built from deserialization
        explicit node_core_impl(load_balancer &p_load_balancer, invokable<Ret, Args...> auto &&p_runnable, std::function<task_packet(Args...)> p_args_serializer,
                                std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) :
            node_core_impl(p_load_balancer, false) {

            m_runnable = std::forward<decltype(p_runnable)>(p_runnable);
            m_args_serializer = p_args_serializer;
            m_args_deserializer = p_args_deserializer;
        }

        // Dummy node
        explicit node_core_impl(load_balancer &p_load_balancer, bool p_is_dummy) :
            m_load_balancer(p_load_balancer) {
            this->m_thread_id = std::this_thread::get_id();
            this->m_node_id = m_load_balancer.generate_unique_id();
            this->m_is_dummy = p_is_dummy;
        }

    public:
        static std::shared_ptr<node_core> create_explicit(load_balancer &p_load_balancer, std::shared_ptr<node_core> &p_parent, invokable<Ret, Args...> auto &&p_runnable,
                                                          std::tuple<Args...> &&p_args) {

            auto v_args_serializer = static_cast<std::function<task_packet(Args...)>>(nullptr);
            auto v_args_deserializer = static_cast<std::function<std::tuple<Args...>(task_packet)>>(nullptr);
            return create_serializable_explicit(p_load_balancer, p_parent, p_runnable, std::move(p_args), v_args_serializer, v_args_deserializer);
        }

        static std::shared_ptr<node_core> create_serializable_explicit(load_balancer &p_load_balancer, std::shared_ptr<node_core> &p_parent,
                                                                       invokable<Ret, Args...> auto &&p_runnable, std::tuple<Args...> &&p_args,
                                                                       std::function<task_packet(Args...)> p_args_serializer,
                                                                       std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {

            auto ptr = new node_core_impl(p_load_balancer, p_runnable, std::move(p_args), p_args_serializer, p_args_deserializer);
            std::shared_ptr<node_core_impl> v_instance = std::shared_ptr<node_core_impl>(ptr);
            v_instance->init(p_parent);
            return v_instance;
        }

        static std::shared_ptr<node_core> create_lazy(load_balancer &p_load_balancer, std::shared_ptr<node_core> &p_parent, invokable<Ret, Args...> auto &&p_runnable,
                                                      std::function<std::optional<std::tuple<Args...>>()> p_args_initializer) {

            auto v_args_serializer = static_cast<std::function<task_packet(Args...)>>(nullptr);
            auto v_args_deserializer = static_cast<std::function<std::tuple<Args...>(task_packet)>>(nullptr);
            return create_serializable_lazy(p_load_balancer, p_parent, p_runnable, p_args_initializer, v_args_serializer, v_args_deserializer);
        }

        static std::shared_ptr<node_core> create_serializable_lazy(load_balancer &p_load_balancer, std::shared_ptr<node_core> &p_parent, invokable<Ret, Args...> auto &&p_runnable,
                                                                   std::function<std::optional<std::tuple<Args...> >()> p_args_initializer,
                                                                   std::function<task_packet(Args...)> p_args_serializer,
                                                                   std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {

            auto ptr = new node_core_impl(p_load_balancer, p_runnable, p_args_initializer, p_args_serializer, p_args_deserializer);
            std::shared_ptr<node_core_impl> v_instance = std::shared_ptr<node_core_impl>(ptr);
            v_instance->init(p_parent);

            return v_instance;
        }


        static std::shared_ptr<node_core> create_serializable(load_balancer &p_load_balancer, std::shared_ptr<node_core> &p_parent, invokable<Ret, Args...> auto &&p_runnable,
                                                              std::function<task_packet(Args...)> p_args_serializer,
                                                              std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
            auto ptr = new node_core_impl(p_load_balancer, p_runnable, p_args_serializer, p_args_deserializer);
            std::shared_ptr<node_core_impl> v_instance = std::shared_ptr<node_core_impl>(ptr);
            v_instance->init(p_parent);

            return v_instance;
        }

        static std::shared_ptr<node_core> create_seed(load_balancer &p_load_balancer, invokable<Ret, Args...> auto &&p_runnable,
                                                      std::function<std::tuple<Args...>()> p_args_initializer) {

            auto v_args_serializer = static_cast<std::function<task_packet(Args...)>>(nullptr);
            auto v_args_deserializer = static_cast<std::function<std::tuple<Args...>(task_packet)>>(nullptr);

            auto ptr = new node_core_impl(p_load_balancer, p_runnable, p_args_initializer, v_args_serializer, v_args_deserializer);
            std::shared_ptr<node_core_impl> v_instance = std::shared_ptr<node_core_impl>(ptr);
            v_instance->init(nullptr);
            return v_instance;
        }

        static std::shared_ptr<node_core> create_dummy(load_balancer &p_load_balancer) {
            auto ptr = new node_core_impl(p_load_balancer, true);
            std::shared_ptr<node_core_impl> v_instance = std::shared_ptr<node_core_impl>(ptr);
            v_instance->init(nullptr);
            return v_instance;
        }

    private:
        void init(const std::shared_ptr<node_core> &p_parent) {

            if (p_parent) {
                set_parent(p_parent);
            } else {
                // if there is no parent, it means the thread just took another subtree,
                // therefore, the root in handler.roots[thread_id] should change since
                // no one else is supposed to be using it

                std::shared_ptr<node_core> v_itself = shared_from_this();
                m_load_balancer.set_root(m_thread_id, v_itself);
                m_root = m_load_balancer.get_root(m_thread_id);
            }
        }

    public:
        ~node_core_impl() override = default;

        // Disable Copy
        node_core_impl(const node_core_impl &) = delete;

        node_core_impl &operator=(const node_core_impl &) = delete;

        // Allow Move
        node_core_impl(node_core_impl &&) noexcept = default;

        node_core_impl &operator=(node_core_impl &&) noexcept = default;

        //<editor-fold desc="Delegable">
        void run() override {
            if (m_state != UNUSED) {
                utils::log_and_throw("node is already consumed, node: {}, state: {}", m_node_id, get_state_string(m_state));
            }
            if (m_initialization_flag == UNINITIALIZED) {
                utils::log_and_throw("node arguments have not been initialized");
            }
            if (m_initialization_flag == LAZY) {
                auto v_opt = m_initializer();
                if (!v_opt.has_value()) {
                    utils::log_and_throw("Attempted to initialize a non-worthy node: this should not happen!, node: {}, state: {}", m_node_id, get_state_string(m_state));
                }
                m_arguments = std::move(v_opt.value());
                m_initialization_flag = INITIALIZED;
            }

            m_result = m_invoke(false);
            m_state = FORWARDED;
        }

        void delegate_locally(load_balancer *p_load_balancer) override {
            if (m_state != UNUSED) {
                utils::log_and_throw("node is already consumed, node: {}, state: {}", m_node_id, get_state_string(m_state));
            }
            if (m_initialization_flag == UNINITIALIZED) {
                utils::log_and_throw("Node arguments have not been initialized");
            }
            if (m_initialization_flag == LAZY) {
                auto v_opt = m_initializer();
                if (!v_opt.has_value()) {
                    utils::log_and_throw("Attempted to initialize a non-worthy node: this should not happen!, node: {}, state: {}", m_node_id, get_state_string(m_state));
                }
                m_arguments = std::move(v_opt.value());
                m_initialization_flag = INITIALIZED;
            }

            m_result = p_load_balancer->force_local_submit([v_copy = shared_from_this(), this] {
                // IMPORTANT: copy keeps "this" alive
                return m_invoke(true);
            });
            m_state = PUSHED;
        }

        void delegate_remotely(scheduler::worker *p_worker, const int p_runner_id) override {
            if (m_state != UNUSED) {
                utils::log_and_throw("node is already consumed, node: {}, state: {}", m_node_id, get_state_string(m_state));
            }
            if (m_initialization_flag == UNINITIALIZED) {
                utils::log_and_throw("Node arguments have not been initialized");
            }
            if (m_initialization_flag == LAZY) {
                auto v_opt = m_initializer();
                if (!v_opt.has_value()) {
                    utils::log_and_throw("Attempted to initialize a non-worthy node: this should not happen!, node: {}, state: {}", m_node_id, get_state_string(m_state));
                }
                m_arguments = std::move(v_opt.value());
                m_initialization_flag = INITIALIZED;
            }
            m_remote_node = p_worker->force_push(serialize(), p_runner_id);
            m_state = SENT_TO_ANOTHER_PROCESS;
        }

        //</editor-fold>

        //<editor-fold desc="Serializable">
        task_packet serialize() override {
            if (m_initialization_flag == UNINITIALIZED || m_initialization_flag == LAZY) {
                utils::log_and_throw("node arguments have not been initialized");
            }
            if (m_args_serializer == nullptr) {
                utils::log_and_throw("Arguments serializer has not been set");
            }
            return std::apply(m_args_serializer, m_arguments);
        }

        void deserialize(const task_packet &p_buffer) override {
            if (m_initialization_flag != UNINITIALIZED) {
                utils::log_and_throw("node arguments have already been initialized");
            }
            if (m_args_deserializer == nullptr) {
                utils::log_and_throw("Arguments deserializer has not been set");
            }
            m_arguments = m_args_deserializer(p_buffer);
            m_initialization_flag = DESERIALIZED;
        }

        //</editor-fold>

        //<editor-fold desc="RemoteResult">
        void set_result(const task_packet &p_result) override {
            if (m_result_deserializer == nullptr) {
                utils::log_and_throw("Result deserializer has not been set");
            }
            m_result = m_result_deserializer(p_result);
        }

        [[nodiscard]] task_packet get_result() override {
            if (m_result_serializer == nullptr) {
                utils::log_and_throw("Result serializer has not been set");
            }
            const std::any &v_any = get_any_result();
            return m_result_serializer(v_any);
        }

        //</editor-fold>

        void set_result_serializer(const std::function<task_packet(std::any)> &p_result_serializer) override { m_result_serializer = p_result_serializer; }

        void set_result_deserializer(const std::function<std::any(task_packet)> &p_result_deserializer) override { m_result_deserializer = p_result_deserializer; }

        bool operator==(const node_core_impl &p_rhs) const = default;

        [[nodiscard]] bool is_dummy() const override { return m_is_dummy; }

        [[nodiscard]] std::thread::id get_thread_id() const override { return m_thread_id; }

        [[nodiscard]] int get_node_id() const override { return m_node_id; }

        [[nodiscard]] int get_forward_count() const override { return m_forward_count; }

        [[nodiscard]] int get_push_count() const override { return m_push_count; }

        void set_state(node_state p_state) override {
            this->m_state = p_state;
            switch (m_state) {
                case FORWARDED: {
                    ++this->m_forward_count;
                    break;
                }
                case PUSHED: {
                    ++this->m_push_count;
                    break;
                }
                default:
                    return;
            }
        }

        [[nodiscard]] node_state get_state() const override { return this->m_state; }

        [[nodiscard]] bool is_result_ready() const override {
            // TODO...
            return this->m_state != DISCARDED && this->m_state != RETRIEVED;
        }

        [[nodiscard]] bool is_consumed() const override { return m_state != UNUSED; }

        bool should_branch() override {
            utils::print_ipc_debug_comments("run_id: {}, should_branch() called, state={}, init_flag={}", m_node_id, get_state_string(m_state), m_initialization_flag);

            if (m_should_branch_cached.has_value()) {
                utils::print_ipc_debug_comments("run_id: {}, should_branch() returning cached value: {}", m_node_id, m_should_branch_cached.value());
                return m_should_branch_cached.value();
            }

            if (m_state == FORWARDED || m_state == PUSHED || m_state == DISCARDED) {
                utils::print_ipc_debug_comments("run_id: {}, should_branch() returning false due to state", m_node_id);
                m_initialization_flag = INITIALIZED;
                m_should_branch_cached = false;
                return false;
            }
            if (m_initialization_flag == INITIALIZED) {
                m_should_branch_cached = true;
                return true;
            }
            if (m_initialization_flag == LAZY) {
                auto v_args_opt = m_initializer();
                utils::print_ipc_debug_comments("run_id: {}, should_branch() calling initializer", m_node_id);
                if (!v_args_opt.has_value()) {
                    utils::print_ipc_debug_comments("run_id: {}, should_branch() initializer returned nullopt", m_node_id);
                    m_should_branch_cached = false;
                    m_initialization_flag = INITIALIZED;
                    return false;
                }
                m_arguments = std::move(v_args_opt.value());
                m_initialization_flag = INITIALIZED;
                m_should_branch_cached = true;
                utils::print_ipc_debug_comments("run_id: {}, should_branch() initializer succeeded, returning true", m_node_id);
                return true;
            }
            m_should_branch_cached = true;
            return true;
        }

        [[nodiscard]] std::shared_ptr<node_core> get_root() override {
            return m_root ? *m_root : nullptr;
        }

        void set_parent(const std::shared_ptr<node_core> &p_parent) override {
            if (m_is_dummy && p_parent != nullptr) {
                utils::log_and_throw("Cannot set parent for a dummy node");
            }
            if (p_parent.get() == this) {
                // Check if the new parent is not this node itself
                utils::log_and_throw("Cannot set self as parent");
            }
            if (!m_children.empty() && p_parent != nullptr) {
                utils::log_and_throw("Cannot set parent to nullptr when children are present");
            }
            if (m_parent.get() == p_parent.get()) {
                // No change in parent, so return early to avoid recursion
                return;
            }

            this->m_parent = p_parent;

            // Ensure the parent adds this node as a child if it's not already there
            if (m_parent != nullptr) {
                const std::shared_ptr<node_core> v_shared = shared_from_this();
                this->m_parent->add_child(v_shared);
                m_root = m_load_balancer.get_root(m_parent->get_thread_id());
            }
        }

        [[nodiscard]] std::shared_ptr<node_core> get_parent() override { return m_parent; }

        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_child() override { return m_children.empty() ? nullptr : m_children.front(); }

        [[nodiscard]] std::shared_ptr<node_core> get_second_leftmost_child() override {
            if (m_children.size() < 2) {
                utils::log_and_throw("Cannot get second child when there are less than 2 children");
            }
            auto it = m_children.begin();
            return *(++it);
        }

        void remove_leftmost_child() override { m_children.pop_front(); }

        void remove_second_leftmost_child() override {
            if (m_children.size() < 2) {
                utils::log_and_throw("Cannot prune second child when there are less than 2 children");
            }
            auto it = m_children.begin();
            m_children.erase(++it);
        }

        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_sibling() override {
            if (m_is_dummy || !m_parent) {
                return nullptr;
            }
            return m_parent->get_leftmost_child();
        }

        [[nodiscard]] std::shared_ptr<node_core> get_left_sibling() override {
            if (m_is_dummy || !m_parent) {
                return nullptr;
            }
            std::list<std::shared_ptr<node_core> > v_parent_children = m_parent->get_children();
            auto v_first = v_parent_children.begin();
            if (this == v_first->get()) {
                return nullptr;
            }
            auto v_it = std::find(v_first, v_parent_children.end(), shared_from_this());
            return *(--v_it);
        }

        [[nodiscard]] std::shared_ptr<node_core> get_right_sibling() override {
            if (m_is_dummy || m_parent == nullptr) {
                return nullptr;
            }
            std::list<std::shared_ptr<node_core> > v_parent_children = m_parent->get_children();
            auto v_first = v_parent_children.begin();
            auto v_last = v_parent_children.end();
            auto v_it = std::find(v_first, v_last, shared_from_this());
            if (this == (--v_last)->get()) {
                return nullptr;
            }
            auto v_temp = *(++v_it);
            return v_temp;
        }

        std::list<std::shared_ptr<node_core> > get_children() override { return m_children; }

        [[nodiscard]] int get_children_count() const override { return m_children.size(); }

        void add_child(const std::shared_ptr<node_core> &p_child) override {
            if (p_child == nullptr || p_child.get() == this) {
                utils::log_and_throw("Cannot add null or self as a child");
            }

            // Set the parent of the child
            if (p_child->get_parent().get() != this) {
                p_child->set_parent(shared_from_this());
            }

            // Add the child to this node's children list if not already added
            if (std::ranges::find(m_children, p_child) == m_children.end()) {
                this->m_children.push_back(p_child);
            }
        }

        std::any get_any_result() override {
            if (std::holds_alternative<std::any>(m_result)) {
                this->m_state = RETRIEVED;
                return std::get<std::any>(m_result);
            }
            if (std::holds_alternative<std::future<std::any> >(m_result)) {
                auto &v_future = std::get<std::future<std::any> >(m_result);
                v_future.wait();
                std::any v_any_result = v_future.get();
                this->m_state = RETRIEVED;
                return v_any_result;
            }
            utils::log_and_throw("Attempting to get result of node that has not been executed");
        }

        void remove_child(std::shared_ptr<node_core> &p_child) override { m_children.remove(p_child); }

        void prune() override {
            if (m_parent) {
                // this node could be a root
                std::shared_ptr<node_core> v_itself = shared_from_this();
                m_parent->remove_child(v_itself);
            }
            m_root = nullptr;
            set_parent(nullptr);
        }

    private:
        // Utilities
        static std::string get_state_string(const node_state& p_state) {
            switch (p_state) {
                case UNUSED:
                    return "UNUSED";
                case FORWARDED:
                    return "FORWARDED";
                case PUSHED:
                    return "PUSHED";
                case DISCARDED:
                    return "DISCARDED";
                case RETRIEVED:
                    return "RETRIEVED";
                case SENT_TO_ANOTHER_PROCESS:
                    return "SENT_TO_ANOTHER_PROCESS";
                default:
                    return "UNKNOWN_STATE";
            }
        }
    };
} // namespace gempba

#endif // GEMPBA_NODE_IMPL_HPP
