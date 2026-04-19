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
#ifndef NODE_BLOCK_HPP
#define NODE_BLOCK_HPP

#include <any>
#include <gempba/core/node_traits.hpp>
#include <gempba/core/scheduler.hpp>
#include <memory>

namespace gempba {
    class node_core : public node_traits<std::shared_ptr<node_core>> {
    protected:
        node_core() = default;

    public:
        ~node_core() override = default;

        // Disable Copy
        // node_core(const node_core&) = delete;
        // node_core& operator=(const node_core&) = delete;
        node_core(const node_core &) = default;

        node_core &operator=(const node_core &) = default;

        // Allow Move
        node_core(node_core &&) noexcept = default;

        node_core &operator=(node_core &&) noexcept = default;

        void run() override = 0;

        void delegate_locally(load_balancer *p_load_balancer) override = 0;

        void delegate_remotely(scheduler::worker *p_scheduler, int p_runner_id) override = 0;

        void set_result(const task_packet &p_result) override = 0;

        task_packet get_result() override = 0;

        task_packet serialize() override = 0;

        void deserialize(const task_packet &p_buffer) override = 0;

        void set_result_serializer(const std::function<task_packet(std::any)> &p_result_serializer) override = 0;

        void set_result_deserializer(const std::function<std::any(task_packet)> &p_result_deserializer) override = 0;

        [[nodiscard]] bool is_dummy() const override = 0;

        [[nodiscard]] std::thread::id get_thread_id() const override = 0;

        [[nodiscard]] int get_node_id() const override = 0;

        [[nodiscard]] int get_forward_count() const override = 0;

        [[nodiscard]] int get_push_count() const override = 0;

        [[nodiscard]] node_state get_state() const override = 0;

        void set_state(node_state p_state) override = 0;

        [[nodiscard]] bool is_consumed() const override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_root() override = 0;

        void set_parent(const std::shared_ptr<node_core> &p_parent) override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_parent() override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_child() override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_second_leftmost_child() override = 0;

        void remove_leftmost_child() override = 0;

        void remove_second_leftmost_child() override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_sibling() override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_left_sibling() override = 0;

        [[nodiscard]] std::shared_ptr<node_core> get_right_sibling() override = 0;

        [[deprecated]] std::list<std::shared_ptr<node_core>> get_children() override = 0;

        [[nodiscard]] int get_children_count() const override = 0;

        void add_child(const std::shared_ptr<node_core> &p_child) override = 0;

        std::any get_any_result() override = 0;

        [[nodiscard]] bool is_result_ready() const override = 0;

        bool should_branch() override = 0;

        void remove_child(std::shared_ptr<node_core> &p_child) override = 0;

        void prune() override = 0;
    };


}; // namespace gempba

#endif // NODE_BLOCK_HPP
