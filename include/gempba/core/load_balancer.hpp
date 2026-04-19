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

#ifndef GEMPBA_LOAD_BALANCER_HPP
#define GEMPBA_LOAD_BALANCER_HPP

#include <any>
#include <future>
#include <gempba/core/node.hpp>
#include <gempba/utils/gempba_utils.hpp>


/**
 * @author Andres Pastrana
 * @date 2025-08-30
 */
namespace gempba {

    class load_balancer {
    protected:
        load_balancer() = default;

    public:
        virtual ~load_balancer() = default;

        virtual std::shared_ptr<std::shared_ptr<node_core>> get_root(std::thread::id p_thread_id) = 0;

        virtual void set_root(std::thread::id p_thread_id, std::shared_ptr<node_core> &p_root) = 0;

        virtual void set_thread_pool_size(unsigned int p_size) = 0;

        virtual balancing_policy get_balancing_policy() = 0;

        virtual unsigned int generate_unique_id() = 0;

        virtual std::future<std::any> force_local_submit(std::function<std::any()> &&p_function) = 0;

        virtual void forward(node &p_node) = 0;

        virtual bool try_local_submit(node &p_node) = 0;

        virtual bool try_remote_submit(node &p_node, int p_runnable_id) = 0;

        [[nodiscard]] virtual double get_idle_time() const = 0;

        virtual void wait() = 0;

        [[nodiscard]] virtual bool is_done() const = 0;

        [[nodiscard]] virtual std::size_t get_thread_request_count() const = 0;
    };
} // namespace gempba

#endif // GEMPBA_LOAD_BALANCER_HPP
