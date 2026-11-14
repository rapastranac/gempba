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
#ifndef GEMPBA_GEMPBA_HPP
#define GEMPBA_GEMPBA_HPP

#include <gempba/core/load_balancer.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/core/stop.hpp>
#include <gempba/detail/nodes/node_core_impl.hpp>
#include <gempba/detail/runnables/serial_runnable_non_void.hpp>
#include <gempba/detail/runnables/serial_runnable_void.hpp>
#include <gempba/export.h>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <gempba/utils/gempba_utils.hpp>

namespace gempba {

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// SCHEDULING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    GEMPBA_API scheduler* get_scheduler();

    GEMPBA_API scheduler* try_get_scheduler() noexcept;

    GEMPBA_API void reset_scheduler();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// LOAD BALANCING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    GEMPBA_API load_balancer* get_load_balancer();

    GEMPBA_API void reset_load_balancer();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// BRANCH HANDLING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    GEMPBA_API node_manager& get_node_manager();

    GEMPBA_API void reset_node_manager();

    GEMPBA_API int shutdown();

    template<typename Ret, typename... Args>
    static node create_seed_node(load_balancer& p_load_balancer, invokable<Ret, Args...> auto&& p_runnable, std::tuple<Args...> p_args) {
        std::shared_ptr<node_core> v_null;
        auto v_seed = node_core_impl<Ret(Args...)>::create_explicit(p_load_balancer, v_null, p_runnable, std::move(p_args));
        return node(v_seed);
    }

    static node create_dummy_node(load_balancer& p_load_balancer) {
        const auto v_core = node_core_impl<void()>::create_dummy(p_load_balancer);
        return node(v_core);
    }

    [[maybe_unused]] static node create_custom_node(const std::shared_ptr<node_core>& p_core) noexcept { return node(p_core); }

    GEMPBA_API void check_not_null([[maybe_unused]] const node& p_parent);

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// LOAD BALANCING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    /** @brief Take ownership of a caller-provided `load_balancer` implementation. */
    GEMPBA_API load_balancer* create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation);

    /**
     * @brief Create the built-in load balancer for @p p_policy.
     *
     * @param p_scheduler The scheduler that sends tasks to other ranks, or nullptr in a single-process run.
     */
    GEMPBA_API load_balancer* create_load_balancer(const balancing_policy& p_policy, scheduler* p_scheduler = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// BRANCH HANDLING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    /**
     * @brief Create the node manager over @p p_load_balancer.
     *
     * @param p_scheduler The scheduler that sends tasks to other ranks, or nullptr in a single-process run.
     */
    GEMPBA_API node_manager& create_node_manager(load_balancer* p_load_balancer, scheduler* p_scheduler = nullptr);

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// SERIAL RUNNABLES
    /// /////////////////////////////////////////////////////////////////////////////////////////////

    namespace runnables::return_none {
        template<typename... Args>
        std::shared_ptr<serial_runnable> create(const int p_id, invokable<void, Args...> auto&& p_invokable,
                                                const std::function<std::tuple<Args...>(const task_packet&&)>& p_args_deserializer) {
            return std::make_shared<serial_runnable_void<void(Args...)>>(p_id, p_invokable, p_args_deserializer);
        }
    } // namespace runnables::return_none

    namespace runnables::return_value {
        template<typename R, typename... Args>
        std::shared_ptr<serial_runnable> create(const int p_id, invokable<R, Args...> auto&& p_invokable, const std::function<std::tuple<Args...>(const task_packet&&)>& p_args_deserializer,
                                                const std::function<task_packet(R)>& p_result_serializer) {
            return std::make_shared<serial_runnable_non_void<R(Args...)>>(p_id, p_invokable, p_args_deserializer, p_result_serializer);
        }
    } // namespace runnables::return_value

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// NODES
    /// /////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Ret, typename... Args>
    static node create_explicit_node(load_balancer& p_load_balancer, node& p_parent, invokable<Ret, Args...> auto&& p_runnable, std::tuple<Args...>&& p_args) {
        check_not_null(p_parent);

        const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
            auto v_core = node_core_impl<Ret(Args...)>::create_explicit(p_load_balancer, p_core_parent, p_runnable, std::move(p_args));
            return v_core;
        };
        return node::create(p_parent, v_factory);
    }

    template<typename Ret, typename... Args>
    static node create_lazy_node(load_balancer& p_load_balancer, const node& p_parent, invokable<Ret, Args...> auto&& p_runnable,
                                 std::function<std::optional<std::tuple<Args...>>()> p_args_initializer) {
        check_not_null(p_parent);

        const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
            auto v_node_core = node_core_impl<Ret(Args...)>::create_lazy(p_load_balancer, p_core_parent, p_runnable, p_args_initializer);
            return v_node_core;
        };

        return node::create(p_parent, v_factory);
    }

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// NODES THAT CAN RUN ON OTHER RANKS
    /// /////////////////////////////////////////////////////////////////////////////////////////////

    template<typename Ret, typename... Args>
    static node create_explicit_node(load_balancer& p_load_balancer, const node& p_parent, invokable<Ret, Args...> auto&& p_runnable, std::tuple<Args...>&& p_args,
                                     std::function<task_packet(Args...)> p_args_serializer, std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
        check_not_null(p_parent);

        const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
            auto v_core = node_core_impl<Ret(Args...)>::create_serializable_explicit(p_load_balancer, p_core_parent, p_runnable, std::move(p_args), p_args_serializer, p_args_deserializer);
            return v_core;
        };
        return node::create(p_parent, v_factory);
    }

    template<typename Ret, typename... Args>
    static node create_lazy_node(load_balancer& p_load_balancer, const node& p_parent, invokable<Ret, Args...> auto&& p_runnable,
                                 std::function<std::optional<std::tuple<Args...>>()> p_args_initializer, std::function<task_packet(Args...)> p_args_serializer,
                                 std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
        check_not_null(p_parent);
        const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
            auto v_node_core = node_core_impl<Ret(Args...)>::create_serializable_lazy(p_load_balancer, p_core_parent, p_runnable, p_args_initializer, p_args_serializer, p_args_deserializer);
            return v_node_core;
        };

        return node::create(p_parent, v_factory);
    }

    template<typename Ret, typename... Args>
    static node create_from_bytes_node(load_balancer& p_load_balancer, const node& p_parent, invokable<Ret, Args...> auto&& p_f, std::function<task_packet(Args...)> p_args_serializer,
                                       std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
        check_not_null(p_parent);

        const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
            auto v_serializable_node_core = node_core_impl<Ret(Args...)>::create_serializable(p_load_balancer, p_core_parent, p_f, p_args_serializer, p_args_deserializer);
            return v_serializable_node_core;
        };
        return node::create(p_parent, v_factory);
    }

} // namespace gempba

#endif // GEMPBA_GEMPBA_HPP
