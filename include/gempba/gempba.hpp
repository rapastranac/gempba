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

#include <load_balancing/api/load_balancer.hpp>
#include <runnables/factory/serial_runnable_factory.hpp>
#include <schedulers/api/scheduler.hpp>
#include <schedulers/defaults/default_mpi_stats_visitor.hpp>
#include <utils/gempba_utils.hpp>

namespace gempba {

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// SCHEDULING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    scheduler *get_scheduler();

    void reset_scheduler();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// LOAD BALANCING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    load_balancer *get_load_balancer();

    void reset_load_balancer();

    /////////////////////////////////////////////////////////////////////////////////////////////////
    /// BRANCH HANDLING
    /// /////////////////////////////////////////////////////////////////////////////////////////////
    branch_handler &get_branch_handler();

    void reset_branch_handler();

    namespace mt {
        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// LOAD BALANCING
        /// /////////////////////////////////////////////////////////////////////////////////////////////
        load_balancer *create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation);

        load_balancer *create_load_balancer(const balancing_policy &p_policy);

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// BRANCH HANDLING
        /// /////////////////////////////////////////////////////////////////////////////////////////////

        branch_handler &create_branch_handler(load_balancer *p_load_balancer);

    }

    namespace mp {

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// SCHEDULING
        /// /////////////////////////////////////////////////////////////////////////////////////////////
        enum scheduler_topology {
            SEMI_CENTRALIZED,
            CENTRALIZED,
        };

        scheduler *create_scheduler(std::unique_ptr<scheduler> p_your_implementation);

        scheduler *create_scheduler(const scheduler_topology &p_topology, double p_timeout = 3.0);

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// LOAD BALANCING
        /// /////////////////////////////////////////////////////////////////////////////////////////////

        load_balancer *create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation);

        load_balancer *create_load_balancer(const balancing_policy &p_policy, scheduler::worker *p_scheduler_worker);

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// STATISTICS
        /// /////////////////////////////////////////////////////////////////////////////////////////////

        std::unique_ptr<default_mpi_stats_visitor> get_default_mpi_stats_visitor();

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// BRANCH HANDLING
        /// /////////////////////////////////////////////////////////////////////////////////////////////
        branch_handler &create_branch_handler(load_balancer *p_load_balancer, scheduler::worker *p_worker);

        /////////////////////////////////////////////////////////////////////////////////////////////////
        /// SERIAL RUNNABLES
        /// /////////////////////////////////////////////////////////////////////////////////////////////

        namespace runnables::return_none {
            template<typename... Args>
            std::shared_ptr<serial_runnable> create(const int p_id,
                                                    invokable<void, Args...> auto &&p_invokable,
                                                    std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer) {
                return serial_runnable_factory::return_none::create(p_id, std::forward<decltype(p_invokable)>(p_invokable), p_args_deserializer);
            }
        }

        namespace runnables::return_value {
            template<typename R, typename... Args>
            std::shared_ptr<serial_runnable> create(const int p_id,
                                                    invokable<R, Args...> auto &&p_invokable,
                                                    std::function<std::tuple<Args...>(const task_packet &&)> p_args_deserializer,
                                                    std::function<task_packet(R)> p_result_serializer) {
                return serial_runnable_factory::return_value::create<R>(p_id, std::forward<decltype(p_invokable)>(p_invokable),
                                                                        p_args_deserializer, p_result_serializer);
            }
        }

    }
}

#endif //GEMPBA_GEMPBA_HPP
