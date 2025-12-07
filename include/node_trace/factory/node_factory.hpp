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

#ifndef GEMPBA_NODE_FACTORY_HPP
#define GEMPBA_NODE_FACTORY_HPP

#include <memory>
#include <optional>
#include <utility>
#include <node_trace/impl/node_core_impl.hpp>
#include <load_balancing/api/load_balancer.hpp>

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {
    class node_factory {
        static void check_not_null([[maybe_unused]] const node& p_parent) {
            #ifdef GEMPBA_TEST_MODE
            if (p_parent == nullptr) {
                spdlog::throw_spdlog_ex("Node creation cannot have a nullptr for a parent");
            }
            #endif
        }

    public:
        /**
         * This factory method instantiates explicitly a Node instance
         * @tparam Ret Return type of the function
         * @tparam Args Arguments of the function
         * @param p_load_balancer
         * @param p_parent Parent node
         * @param p_runnable invokable function that receives the arguments and returns the type Ret
         * @param p_args explicitly passed arguments
         * @return The created Node
         */
        template <typename Ret, typename... Args>
        static node create_explicit_node(load_balancer& p_load_balancer,
                                         node& p_parent,
                                         invokable<Ret, Args...> auto&& p_runnable,
                                         std::tuple<Args...>&& p_args) {

            check_not_null(p_parent);

            const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
                auto v_core = node_core_impl<Ret(Args...)>::create_explicit(p_load_balancer, p_core_parent, p_runnable, std::move(p_args));
                return v_core;
            };
            return node::create(p_parent, v_factory);
        }

        template <typename Ret, typename... Args>
        static node create_serializable_explicit_node(load_balancer& p_load_balancer,
                                                      node p_parent,
                                                      invokable<Ret, Args...> auto&& p_runnable,
                                                      std::tuple<Args...>&& p_args,
                                                      std::function<task_packet(Args...)> p_args_serializer,
                                                      std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
            check_not_null(p_parent);

            const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
                auto v_core = node_core_impl<Ret(Args...)>::create_serializable_explicit(p_load_balancer, p_core_parent, p_runnable, std::move(p_args), p_args_serializer,
                                                                                         p_args_deserializer);
                return v_core;
            };
            return node::create(p_parent, v_factory);
        }

        /**
         * This factory method creates a Node, whose arguments will be lazily initialized
         * @tparam Ret Return type of the function
         * @tparam Args Arguments of the function
         * @param p_load_balancer
         * @param p_parent Parent node
         * @param p_runnable invokable function that receives the arguments and returns the type Ret
         * @param p_args_initializer Function that will lazily initialize the arguments
         * @return A shared pointer to the created Node
         */
        template <typename Ret, typename... Args>
        static node create_lazy_node(load_balancer& p_load_balancer,
                                     node p_parent,
                                     invokable<Ret, Args...> auto&& p_runnable,
                                     std::function<std::optional<std::tuple<Args...>>()> p_args_initializer) {

            check_not_null(p_parent);
            const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
                auto v_node_core = node_core_impl<Ret(Args...)>::create_lazy(p_load_balancer, p_core_parent, p_runnable, p_args_initializer);
                return v_node_core;
            };

            return node::create(p_parent, v_factory);
        }


        template <typename Ret, typename... Args>
        static node create_serializable_lazy_node(load_balancer& p_load_balancer,
                                                  node p_parent,
                                                  invokable<Ret, Args...> auto&& p_runnable,
                                                  std::function<std::optional<std::tuple<Args...>>()> p_args_initializer,
                                                  std::function<task_packet(Args...)> p_args_serializer,
                                                  std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {

            check_not_null(p_parent);
            const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {
                auto v_node_core = node_core_impl<Ret(Args...)>::create_serializable_lazy(p_load_balancer, p_core_parent, p_runnable, p_args_initializer, p_args_serializer,
                                                                                          p_args_deserializer);
                return v_node_core;
            };

            return node::create(p_parent, v_factory);
        }

        template <typename Ret, typename... Args>
        static node create_seed_node(load_balancer& p_load_balancer,
                                     invokable<Ret, Args...> auto&& p_runnable,
                                     std::function<std::tuple<Args...>()> p_args_initializer) {

            auto v_seed = node_core_impl<Ret(Args...)>::create_seed(p_load_balancer, p_runnable, p_args_initializer);
            return node(v_seed);
        }

        /**
         * This factory method creates a Node, whose arguments will be initialized by a deserializer
         * @tparam Ret Return type of the function
         * @tparam Args Arguments of the function
         * @param p_load_balancer
         * @param p_parent Parent node
         * @param p_f invokable function that receives the arguments and returns the type Ret
         * @param p_args_serializer Function that will serialize the arguments to a string
         * @param p_args_deserializer Function that will deserialize the arguments from a string
         * @return A shared pointer to the created Node
         */
        template <typename Ret, typename... Args>
        static node create_serializable_node(load_balancer& p_load_balancer,
                                             node p_parent,
                                             invokable<Ret, Args...> auto&& p_f,
                                             std::function<task_packet(Args...)> p_args_serializer,
                                             std::function<std::tuple<Args...>(task_packet)> p_args_deserializer) {
            check_not_null(p_parent);

            const std::function<std::shared_ptr<node_core>(std::shared_ptr<node_core>)> v_factory = [&](std::shared_ptr<node_core> p_core_parent) {

                auto v_serializable_node_core = node_core_impl<Ret(Args...)>::create_serializable(p_load_balancer, p_core_parent, p_f, p_args_serializer, p_args_deserializer);
                return v_serializable_node_core;
            };
            return node::create(p_parent, v_factory);
        }

        /**
         * This factory method creates a dummy Node that will serve as a parent of other Node instances. This
         * dummy Node is only an anchor, and does not contain arguments.
         * @param p_load_balancer
         * @return A shared pointer to the created Node
         */
        static node create_dummy_node(load_balancer& p_load_balancer) {
            const auto v_core = node_core_impl<void()>::create_dummy(p_load_balancer);
            return node(v_core);
        }
    };
} // namespace gempba
#endif // GEMPBA_NODE_FACTORY_HPP
