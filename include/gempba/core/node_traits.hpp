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
#ifndef NODE_TRAITS_HPP
#define NODE_TRAITS_HPP

#include <any>
#include <functional>
#include <list>
#include <thread>

#include <gempba/core/serializable.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/utils/task_packet.hpp>

namespace gempba {

    class load_balancer;

    enum node_state {
        UNUSED, // Node hasn't been used yet
        FORWARDED, // Data is forwarded within the system
        PUSHED, // Data is pushed to another thread within the same process
        DISCARDED, // Data is discarded within the system
        RETRIEVED, // Result is retrieved from the data
        SENT_TO_ANOTHER_PROCESS // Data is sent to another process
    };

    template<typename T>
    class node_traits : public serializable {
    protected:
        node_traits() = default;

    public:
        ~node_traits() override = default;

        virtual void set_result_serializer(const std::function<task_packet(std::any)> &p_result_serializer) = 0;

        virtual void set_result_deserializer(const std::function<std::any(task_packet)> &p_result_deserializer) = 0;

        [[nodiscard]] virtual bool is_dummy() const = 0;

        /**
         * Get the thread id that initialized this node. This is the thread that is executing the function call.
         * @return thread id
         */
        [[nodiscard]] virtual std::thread::id get_thread_id() const = 0;

        /**
         * For debugging purposes only. This is the unique identifier of the node, this is truly unique within the thread,
         * and it is not shared between threads.
         * @return the unique identifier of the node
         */
        [[nodiscard]] virtual int get_node_id() const = 0;

        /**
         * For debugging purposes only. It should not greater than one.
         *
         * @return the number of times this node has been forwarded sequentially
         */
        [[nodiscard]] virtual int get_forward_count() const = 0;

        /**
         * For debugging purposes only. It should not greater than one.
         * @return the number of times this node has been pushed to another thread
         */
        [[nodiscard]] virtual int get_push_count() const = 0;

        [[nodiscard]] virtual node_state get_state() const = 0;

        virtual void set_state(node_state p_state) = 0;

        [[nodiscard]] virtual bool is_consumed() const = 0;

        /**
         * @brief This is visible so the load_balancer implementation can use it
         * @return the root of the tree
         */
        virtual T get_root() = 0;

        /**
         * Set the parent of this node, if it is not a dummy (i.e., it has arguments). If it is a dummy,
         * it will throw a runtime_error exception. The root of the parent node will be set to the root of this node.
         *
         * @brief This is visible to allow hierarchy manipulation between implementations
         */
        virtual void set_parent(const T &p_parent) = 0;

        /**
         * Get the parent of this node, if it is not a dummy (i.e., it has arguments). If it is a dummy,
         * it will return nullptr.
         * @return the parent of this node, nullptr otherwise
         */
        [[nodiscard]] virtual T get_parent() = 0;

        /**
         * The first child of this node, which is also the <code>leftMost</code> child.
         * @return the first child of this node
         */
        [[nodiscard]] virtual T get_leftmost_child() = 0;

        /**
         * The second child of this node.
         * @return the second child of this node
         */
        [[nodiscard]] virtual T get_second_leftmost_child() = 0;

        /**
         * Removes the first child of this node.
         */
        virtual void remove_leftmost_child() = 0;

        /**
         * Removes the second child of this node. If the node has only one child, it will throw a runtime_error exception.
         */
        virtual void remove_second_leftmost_child() = 0;

        /**
         * If this node is the <code>leftMost</code> child of its parent node, it will return itself. Otherwise,
         * it will return the <code>leftMost</code> child of its parent node. If this node is a dummy, it will return nullptr.
         * @return the first sibling of this node
         */
        [[nodiscard]] virtual T get_leftmost_sibling() = 0;

        /**
         * It will return the second child of its parent node. If this node is a dummy, it will return nullptr.
         * If this node is the first child of its parent, it will return nullptr.
         * @return previous sibling of this node
         */
        [[nodiscard]] virtual T get_left_sibling() = 0;

        /**
         * It will return the next sibling of this node. If this node is the last child of its parent, it will return nullptr.
         * If this node is a dummy, it will return nullptr.
         * @return next sibling of this node
         */
        [[nodiscard]] virtual T get_right_sibling() = 0;

        [[deprecated("Internal use only")]] virtual std::list<T> get_children() = 0;

        [[nodiscard]] virtual int get_children_count() const = 0;

        virtual void add_child(const T &p_child) = 0;

        virtual std::any get_any_result() = 0;

        [[nodiscard]] virtual bool is_result_ready() const = 0;

        /**
         * This should be invoked always before calling a branch, since
         * it invokes user's instructions to prepare data that will be pushed
         * If not invoked, input for a specific branch handled by the Node instance
         * will be empty.
         *
         * This method allows always having input data ready before a branch call, avoiding having
         * data in the stack before it is actually needed.
         *
         * Thus, the user can evaluate any condition to check if a branch call is worth it or
         * not, while creating temporarily an input data set.
         *
         * If user's condition is met, input is initialized lazily, otherwise, it is not.
         *
         * If a void function is used, this should be a problem, since (WHY THIS???)
         *
         * @return true if the branch represented by this Node is worth exploring, false otherwise
         */
        virtual bool should_branch() = 0;

        virtual void remove_child(T &p_node) = 0;

        virtual void prune() = 0;

        //Delegable ↓↓↓

        /**
        * The underlying task is executed in the current thread.
        */
        virtual void run() = 0;

        /**
         * The underlying task is delegated to another thread by the means of the load_balancer.
         * @param p_load_balancer
         */
        virtual void delegate_locally(load_balancer *p_load_balancer) = 0;

        /**
        * The underlying task is delegated to a remote process by the means of the Scheduler
        * @param p_scheduler
        * @param p_runner_id the id of the runner that will execute the task remotely
        *
        * @return true if the task was successfully delegated, false otherwise.
        */
        virtual void delegate_remotely(scheduler::worker *p_scheduler, int p_runner_id) = 0;

        virtual void set_result(const task_packet &p_result) = 0;

        virtual task_packet get_result() = 0;

    };

}; // namespace gempba

#endif // NODE_TRAITS_HPP
