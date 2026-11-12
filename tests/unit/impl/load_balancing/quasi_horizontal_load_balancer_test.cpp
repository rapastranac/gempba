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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include <gempba/core/scheduler.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/detail/nodes/node_core_impl.hpp>
#include <gempba/node_manager.hpp>
#include <gempba/stats/stats.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <scheduler_mock.hpp>

using namespace std::chrono_literals;

/**
 * @author Andres Pastrana
 * @date 2025-08-30
 */


class quasi_horizontal_load_balancer_test : public ::testing::Test {
protected:
    void SetUp() override { m_scheduler_mock = new gempba::testing_support::scheduler_mock(); }

    void TearDown() override { delete m_scheduler_mock; }

    gempba::testing_support::scheduler_mock* m_scheduler_mock{};
};


TEST_F(quasi_horizontal_load_balancer_test, get_strategy) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    EXPECT_EQ(v_load_balancer.get_balancing_policy(), gempba::QUASI_HORIZONTAL);
};

TEST_F(quasi_horizontal_load_balancer_test, generate_unique_id) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    const unsigned int v_id1 = v_load_balancer.generate_unique_id();
    const unsigned int v_id2 = v_load_balancer.generate_unique_id();
    EXPECT_NE(v_id1, v_id2);
}

TEST_F(quasi_horizontal_load_balancer_test, constructor_returns_with_every_thread_idle) {
    for (unsigned v_i = 0; v_i < 1000; ++v_i) {
        gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
        ASSERT_EQ(0u, v_load_balancer.get_tasks_running_count()) << "iteration " << v_i;
    }
}

TEST_F(quasi_horizontal_load_balancer_test, set_thread_pool_size_returns_with_every_thread_idle) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    for (unsigned v_i = 0; v_i < 1000; ++v_i) {
        v_load_balancer.set_thread_pool_size(1 + v_i % 4);
        ASSERT_EQ(0u, v_load_balancer.get_tasks_running_count()) << "iteration " << v_i;
    }
}

TEST_F(quasi_horizontal_load_balancer_test, set_thread_pool_size_resizes_pool_and_keeps_it_usable) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);

    std::atomic<int> v_runs{0};
    const auto v_submit = [&] {
        return v_load_balancer.force_local_submit([&v_runs]() -> std::any {
            ++v_runs;
            return std::any{};
        });
    };

    v_load_balancer.set_thread_pool_size(4);
    auto v_first = v_submit();
    v_first.wait();
    EXPECT_EQ(1, v_runs.load());

    v_load_balancer.set_thread_pool_size(2);
    auto v_second = v_submit();
    v_second.wait();
    EXPECT_EQ(2, v_runs.load());
}

TEST_F(quasi_horizontal_load_balancer_test, get_idle_time_counts_threads_that_are_still_asleep) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    v_load_balancer.set_thread_pool_size(2);
    const double v_before = v_load_balancer.get_idle_time();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_GE(v_load_balancer.get_idle_time() - v_before, 0.25);
}

TEST_F(quasi_horizontal_load_balancer_test, get_starved_idle_time_grows_while_no_task_runs) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    v_load_balancer.set_thread_pool_size(2);
    const double v_starved_before = v_load_balancer.get_starved_idle_time();
    const double v_imbalanced_before = v_load_balancer.get_imbalanced_idle_time();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_GE(v_load_balancer.get_starved_idle_time() - v_starved_before, 0.25);
    EXPECT_DOUBLE_EQ(v_imbalanced_before, v_load_balancer.get_imbalanced_idle_time());
}

TEST_F(quasi_horizontal_load_balancer_test, get_imbalanced_idle_time_grows_while_one_task_runs) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);
    v_load_balancer.set_thread_pool_size(4);
    std::promise<void> v_started;
    std::future<void> v_started_future = v_started.get_future();
    std::promise<void> v_release;
    std::shared_future<void> v_released = v_release.get_future().share();
    auto v_task = v_load_balancer.force_local_submit([&v_started, v_released]() -> std::any {
        v_started.set_value();
        v_released.wait();
        return {};
    });
    v_started_future.wait();
    const double v_starved_before = v_load_balancer.get_starved_idle_time();
    const double v_imbalanced_before = v_load_balancer.get_imbalanced_idle_time();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const double v_starved_after = v_load_balancer.get_starved_idle_time();
    const double v_imbalanced_after = v_load_balancer.get_imbalanced_idle_time();
    v_release.set_value();
    v_task.wait();
    EXPECT_GE(v_imbalanced_after - v_imbalanced_before, 0.2);
    EXPECT_DOUBLE_EQ(v_starved_before, v_starved_after);
}

TEST_F(quasi_horizontal_load_balancer_test, wait_blocks_until_submitted_task_completes) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_mock);

    std::atomic<bool> v_ran{false};
    auto v_future = v_load_balancer.force_local_submit([&v_ran]() -> std::any {
        v_ran = true;
        return std::any{};
    });

    v_load_balancer.wait();
    EXPECT_TRUE(v_ran.load());
    EXPECT_TRUE(v_load_balancer.is_done());
}


TEST_F(quasi_horizontal_load_balancer_test, set_get_root) {
    const auto v_balancer = new gempba::quasi_horizontal_load_balancer(m_scheduler_mock);
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    int v_dummy_value = 7;

    /**
     * The following lines should not be instantiated in practice, yet this is only for testing purposes
     * every node is spawned on a different thread because there is only one root per thread
     * this is done with the sole purpose of testing the parent/child association
     */
    std::mutex v_vec_mutex;
    std::vector<std::thread::id> v_thread_ids;
    std::vector<std::shared_ptr<gempba::node_core>> v_nodes;
    std::vector<std::thread> v_threads;
    v_threads.reserve(3);
    for (int i = 0; i < 3; ++i) {
        v_threads.emplace_back([&]() {
            std::scoped_lock v_lock(v_vec_mutex);

            v_thread_ids.emplace_back(std::this_thread::get_id());

            std::shared_ptr<gempba::node_core> v_parent = nullptr;
            v_nodes.emplace_back(gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value)));
        });
    }
    for (auto& v_thread: v_threads) {
        v_thread.join();
    }

    const std::thread::id v_tid_1 = v_thread_ids[0];
    const std::thread::id v_tid_2 = v_thread_ids[1];
    const std::thread::id v_tid_3 = v_thread_ids[2];

    auto v_root1 = v_nodes[0];
    auto v_root2 = v_nodes[1];
    auto v_root3 = v_nodes[2];


    v_balancer->set_root(v_tid_1, v_root1);
    v_balancer->set_root(v_tid_2, v_root2);
    v_balancer->set_root(v_tid_3, v_root3);

    const auto v_actual_root1 = v_balancer->get_root(v_tid_1);
    const auto v_actual_root2 = v_balancer->get_root(v_tid_2);
    const auto v_actual_root3 = v_balancer->get_root(v_tid_3);

    ASSERT_EQ(v_root1, *v_actual_root1);
    ASSERT_EQ(v_root2, *v_actual_root2);
    ASSERT_EQ(v_root3, *v_actual_root3);

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, get_unique_id) {
    auto v_balancer = new gempba::quasi_horizontal_load_balancer(m_scheduler_mock);

    ASSERT_EQ(1, v_balancer->generate_unique_id());
    ASSERT_EQ(2, v_balancer->generate_unique_id());
    ASSERT_EQ(3, v_balancer->generate_unique_id());
    ASSERT_EQ(4, v_balancer->generate_unique_id());

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_level_pending_node) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);

    auto v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_parent);
    ASSERT_EQ(nullptr, v_top_node);

    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_child1);
    ASSERT_EQ(nullptr, v_top_node);


    /**
     * From this point onwards:
     * @code
     *           root != dummy
     *            |  \  \   \
     *            |   \    \    \
     *            |    \      \     \
     *            |     \        \      \
     *         child1  child2   child3   child4
     *            |
     *       grandChild1    <-- this level
     *
     * @endcode
     *
     */

    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child4 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    auto v_grand_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));
    v_child1->set_state(gempba::RUNNING);

    v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_grand_child1);
    ASSERT_EQ(v_child2, v_top_node);

    v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_grand_child1);
    ASSERT_EQ(v_child3, v_top_node);

    v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_grand_child1);
    ASSERT_EQ(v_child4, v_top_node);

    // get_root_level_pending_node no longer corrects root

    // grandChild1 should become the new root
    ASSERT_EQ(v_parent, v_grand_child1->get_root());
    ASSERT_EQ(v_child1, v_grand_child1->get_parent());

    auto v_node = gempba::node(v_grand_child1);
    v_balancer.try_correct_root(v_node);

    ASSERT_EQ(v_node, v_node.get_root());
    ASSERT_EQ(nullptr, v_node.get_parent());

    // root and all children should be pruned
    ASSERT_EQ(0, v_parent->get_children_count());
    ASSERT_EQ(0, v_child1->get_children_count());
}

TEST_F(quasi_horizontal_load_balancer_test, thread_request_count_tracking) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    EXPECT_EQ(0, v_balancer.get_thread_request_count());

    // Submit tasks and verify counter increments
    std::function<std::any()> v_dummy_task = []() -> std::any { return 42; };

    auto v_unused_future1 = v_balancer.force_local_submit(std::move(v_dummy_task));
    EXPECT_EQ(1, v_balancer.get_thread_request_count());

    auto v_unused_future2 = v_balancer.force_local_submit([]() -> std::any { return 24; });
    EXPECT_EQ(2, v_balancer.get_thread_request_count());

    v_balancer.wait();
}

TEST_F(quasi_horizontal_load_balancer_test, forward_with_root_correction) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, const gempba::node&) {
        // Simple function that does nothing but allows node execution
    };
    int v_dummy_value = 7;

    // Create a structure where parent == root with multiple children
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_EQ(3, v_parent->get_children_count());
    ASSERT_EQ(v_parent, v_child1->get_parent());
    ASSERT_EQ(v_parent, v_child1->get_root());

    // Forward child1 - this should NOT correct the root
    gempba::node v_node_wrapper(v_child1);
    v_balancer.forward(v_node_wrapper);

    // After forward, child1 should be pruned
    EXPECT_EQ(nullptr, v_child1->get_parent());
    EXPECT_EQ(nullptr, v_child1->get_root());

    // Parent should have fewer children
    EXPECT_EQ(v_parent->get_children_count(), 2);

    // Forward child2 - this SHOULD correct the root
    gempba::node v_node_wrapper2(v_child2);
    v_balancer.forward(v_node_wrapper2);

    // After forward, child2 should be pruned
    EXPECT_EQ(nullptr, v_child2->get_parent());
    EXPECT_EQ(nullptr, v_child2->get_root());

    // Parent should be pruned
    EXPECT_EQ(v_parent->get_children_count(), 0);
    EXPECT_EQ(nullptr, v_parent->get_root());

    // child3 should be the new root
    EXPECT_EQ(nullptr, v_child3->get_parent());
    EXPECT_EQ(v_child3, v_child3->get_root());
}

TEST_F(quasi_horizontal_load_balancer_test, node_pruning_before_delegation) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, const gempba::node&) { v_function_called = true; };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    const auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    const auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    // Set up thread pool with available slots
    v_balancer.set_thread_pool_size(2);

    ASSERT_EQ(v_parent, v_child1->get_parent());
    ASSERT_EQ(v_parent, v_child1->get_root());
    ASSERT_EQ(2, v_parent->get_children_count());

    // Create node wrapper and attempt local submit
    gempba::node v_node_wrapper(v_child1);

    // This should prune the node before delegation
    const bool v_submitted = v_balancer.try_local_submit(v_node_wrapper);

    // Wait for any pending operations
    v_balancer.wait();

    EXPECT_TRUE(v_function_called);

    // Verify the node was properly pruned regardless of submission outcome
    if (v_submitted) {
        // If submitted, node should be pruned
        EXPECT_EQ(nullptr, v_child1->get_parent());
        EXPECT_EQ(nullptr, v_child1->get_root());

        // parent was pruned as it no longer holds two children
        EXPECT_EQ(0, v_parent->get_children_count());

        // child2 becomes the new root
        EXPECT_EQ(nullptr, v_child2->get_parent());
        EXPECT_EQ(v_child2, v_child2->get_root());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, top_node_pruning_in_push_operations_locally) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, const gempba::node&) { v_function_called = true; };
    int v_dummy_value = 7;

    // Create multi-level structure to trigger top node finding
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));

    v_child1->set_state(gempba::FORWARDED); // emulates a forwarded branch

    // Create grandchild to trigger top node search
    const auto v_grandchild = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));

    v_balancer.set_thread_pool_size(2);

    ASSERT_EQ(3, v_root->get_children_count());
    ASSERT_EQ(v_root, v_grandchild->get_root());

    // This should find a top node (child2) and prune it before delegation
    gempba::node v_node_wrapper(v_grandchild);
    const bool v_result = v_balancer.try_push_root_level_node_locally(v_node_wrapper);

    v_balancer.wait();

    if (v_result) {
        ASSERT_TRUE(v_function_called);
        // Verify that the found top node was properly pruned
        EXPECT_EQ(nullptr, v_child2->get_parent());
        EXPECT_EQ(nullptr, v_child2->get_root());

        EXPECT_EQ(2, v_root->get_children_count());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, top_node_pruning_in_push_operations_remotely) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    EXPECT_CALL(*m_scheduler_mock, force_push(testing::_, testing::_)).WillOnce([](gempba::task_packet&& p_task, const int p_function_id) {
        EXPECT_EQ(0, p_function_id);
        EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
        return 999;
    });

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, const gempba::node&) {
        // This is supposed to be run remotely, but mocking doesn't include it for this test
        FAIL();
    };
    int v_dummy_value = 7;

    const std::function<gempba::task_packet(int)> v_dummy_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<int(gempba::task_packet)> v_dummy_deserializer = [](const gempba::task_packet&) { return 42; };

    // Create multi-level structure to trigger top node finding
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 =
            gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child2 =
            gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child3 =
            gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

    v_child1->set_state(gempba::FORWARDED); // emulates a forwarded branch

    // Create grandchild to trigger top node search
    const auto v_grandchild =
            gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

    v_balancer.set_thread_pool_size(2);

    ASSERT_EQ(3, v_root->get_children_count());
    ASSERT_EQ(v_root, v_grandchild->get_root());

    // This should find a top node (child2) and prune it before delegation
    gempba::node v_node_wrapper(v_grandchild);
    const bool v_result = v_balancer.try_push_root_level_node_remotely(v_node_wrapper, 0);

    v_balancer.wait();

    if (v_result) {
        // Verify that the found top node was properly pruned
        EXPECT_EQ(nullptr, v_child2->get_parent());
        EXPECT_EQ(nullptr, v_child2->get_root());

        EXPECT_EQ(2, v_root->get_children_count());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, memory_leak_prevention_through_early_pruning_locally) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, const gempba::node&) {
        // Simple execution that completes quickly
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Create a structure that would be prone to memory leaks
    std::vector<std::shared_ptr<gempba::node_core>> v_nodes;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);

    // Create multiple child nodes
    for (int i = 0; i < 5; ++i) {
        auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
        v_nodes.push_back(v_child);
    }

    // Set up thread pool with available slots
    v_balancer.set_thread_pool_size(3);
    v_balancer.wait(); // it gives time for threads to reach the condition_variable

    // Submit nodes and verify they get pruned
    const size_t v_initial_children = v_parent->get_children_count();
    EXPECT_EQ(5, v_initial_children);

    // Submit several nodes
    for (size_t i = 0; i < 3 && i < v_nodes.size(); ++i) {
        gempba::node v_wrapper(v_nodes[i]);
        bool v_submitted = v_balancer.try_local_submit(v_wrapper);
        EXPECT_TRUE(v_submitted) << "Failed at index: " << i;
    }

    // Wait for processing
    v_balancer.wait();

    // Verify that submitted nodes were pruned (parent/root set to nullptr)
    int v_pruned_count = 0;
    for (const auto& v_node: v_nodes) {
        if (v_node->get_parent() == nullptr && v_node->get_root() == nullptr) {
            v_pruned_count++;
        }
    }

    // At least some nodes should have been pruned
    EXPECT_GT(v_pruned_count, 0);
}

TEST_F(quasi_horizontal_load_balancer_test, memory_leak_prevention_through_early_pruning_remotely) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::mutex v_dummy_mutex;
    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).Times(3).WillRepeatedly([&]() {
        std::unique_lock v_lock(v_dummy_mutex);
        gempba::transmission_guard v_guard(std::move(v_lock));
        return v_guard;
    });

    EXPECT_CALL(*m_scheduler_mock, force_push(testing::_, testing::_))
            .WillOnce([](gempba::task_packet&& p_task, const int p_function_id) {
                EXPECT_EQ(0, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            })
            .WillOnce([](gempba::task_packet&& p_task, const int p_function_id) {
                EXPECT_EQ(1, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            })
            .WillOnce([](gempba::task_packet&& p_task, const int p_function_id) {
                EXPECT_EQ(2, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            });


    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, const gempba::node&) {
        // This is supposed to be run remotely, but mocking doesn't include it for this test
        FAIL();
    };
    int v_dummy_value = 7;

    const std::function<gempba::task_packet(int)> v_dummy_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<int(gempba::task_packet)> v_dummy_deserializer = [](const gempba::task_packet&) { return 42; };

    // Create a structure that would be prone to memory leaks
    std::vector<std::shared_ptr<gempba::node_core>> v_nodes;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);

    // Create multiple child nodes
    for (int i = 0; i < 5; ++i) {
        auto v_child = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value), v_dummy_serializer,
                                                                                       v_dummy_deserializer);
        v_nodes.push_back(v_child);
    }

    // Submit nodes and verify they get pruned
    const size_t v_initial_children = v_parent->get_children_count();
    EXPECT_EQ(5, v_initial_children);

    // Submit several nodes
    for (size_t i = 0; i < 3 && i < v_nodes.size(); ++i) {
        gempba::node v_wrapper(v_nodes[i]);
        bool v_submitted = v_balancer.try_remote_submit(v_wrapper, static_cast<int>(i));
        EXPECT_TRUE(v_submitted);
    }

    // Wait for processing
    v_balancer.wait();

    // Verify that submitted nodes were pruned (parent/root set to nullptr)
    int v_pruned_count = 0;
    for (const auto& v_node: v_nodes) {
        if (v_node->get_parent() == nullptr && v_node->get_root() == nullptr) {
            v_pruned_count++;
        }
    }

    // At least some nodes should have been pruned
    EXPECT_GT(v_pruned_count, 0);
}

TEST_F(quasi_horizontal_load_balancer_test, missing_root_correction_after_pruning_in_send) {
    auto* v_balancer = new gempba::quasi_horizontal_load_balancer();

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, const gempba::node&) {
        // Simple function that completes quickly
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Create structure with virtual root and two children
    // This mimics the initial branching scenario
    auto v_virtual_root = gempba::node_core_impl<void()>::create_dummy(*v_balancer);
    auto v_left_child = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_right_child = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));

    // Create deeper tree under right_child (simulating sequential execution)
    auto v_grandchild_a = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left_child, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_grandchild_b = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left_child, v_dummy_function, std::make_tuple(v_dummy_value));

    /**
     * Structure:
     *     virtual_root (dummy)
     *          /               \
     *     left_child        right_child (executing)
     *     /         \
     * grandchild_A  grandchild_B
     */

    ASSERT_EQ(2, v_virtual_root->get_children_count());
    ASSERT_EQ(v_virtual_root, v_left_child->get_root());
    ASSERT_EQ(v_virtual_root, v_right_child->get_root());
    ASSERT_EQ(v_virtual_root, v_grandchild_a->get_root());

    v_balancer->set_thread_pool_size(4);
    v_balancer->wait(); // give time for threads to reach the condition_variable

    v_left_child->set_state(gempba::FORWARDED); // Emulate that right_child is executing sequentially

    // Now attempt to push grandchild_A
    // This should:
    // 1. get_root_level_pending_node(grandchild_A) returns right_child (top node at root level)
    // 2. right_child gets pushed
    // 3. right_child is pruned from virtual_root
    // 4. virtual_root now has only 1 child (right_child)
    // 5. WITHOUT THE FIX: virtual_root stays as root with 1 child
    // 6. WITH THE FIX: Root should be corrected

    gempba::node v_wrapper_a(v_grandchild_a);
    bool v_submitted_a = v_balancer->try_local_submit(v_wrapper_a);

    v_balancer->wait();

    // After the submission, right_child should be pruned
    EXPECT_EQ(nullptr, v_right_child->get_parent());
    EXPECT_EQ(nullptr, v_right_child->get_root());

    // The critical check: virtual_root should now have been corrected
    // It should either have 0 children (if right_child became new root)
    // OR right_child should be the new root

    if (v_submitted_a) {
        // Check that the root was corrected
        int v_virtual_root_children = v_virtual_root->get_children_count();

        // WITHOUT FIX: This would be 1 (only left_child remains)
        // WITH FIX: This should be 0 (left_child became new root and was removed)
        EXPECT_EQ(0, v_virtual_root_children) << "Root should have been corrected after pruning left_child";

        // Verify right_grandchild became the new root
        EXPECT_EQ(gempba::PUSHED, v_right_child->get_state());
        EXPECT_EQ(gempba::FORWARDED, v_left_child->get_state());
        EXPECT_EQ(gempba::PUSHED, v_grandchild_a->get_state());
        EXPECT_EQ(gempba::UNUSED, v_grandchild_b->get_state());

        EXPECT_EQ(nullptr, v_left_child->get_parent());
        EXPECT_EQ(0, v_left_child->get_children_count());
        EXPECT_EQ(nullptr, v_grandchild_a->get_parent());
        EXPECT_EQ(nullptr, v_grandchild_b->get_parent());
        EXPECT_EQ(v_grandchild_b, v_grandchild_b->get_root());
    }

    // Now try to push grandchild_B - this is where the bug manifests
    // get_root_level_pending_node(grandchild_B) would try to access virtual_root
    // which now has < 2 children and would hit the error
    gempba::node v_wrapper_b(v_grandchild_b);

    // WITHOUT FIX: This would throw the error about virtual root with 1 child
    // WITH FIX: This should work correctly
    EXPECT_NO_THROW({ v_balancer->try_local_submit(v_wrapper_b); });

    v_balancer->wait();

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, reproduce_segfault_scenario_with_virtual_root) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::atomic v_execution_count{0};
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&v_execution_count](std::thread::id, int, const gempba::node&) {
        ++v_execution_count;
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Simulate the exact scenario from the crash log:
    // - Virtual root (dummy)
    // - Deep tree with multiple levels
    // - Rapid parallel submissions that cause root children to be pruned

    auto v_virtual_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_node_l1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_r1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));

    // Build deeper tree under R1
    auto v_node_l2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_r1, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_r2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_r1, v_dummy_function, std::make_tuple(v_dummy_value));

    // Even deeper
    auto v_node_l3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_l2, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_r3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_l2, v_dummy_function, std::make_tuple(v_dummy_value));

    /**
     * Structure:
     *         virtual_root (dummy)
     *              /      \
     *          node_L1   node_R1
     *          /      \
     *      node_L2   node_R2
     *     /      \
     *  node_L3   node_R3
     */

    ASSERT_EQ(v_virtual_root, v_node_l3->get_root());
    ASSERT_TRUE(v_virtual_root->is_dummy());

    v_balancer.set_thread_pool_size(3);

    // Push from deep level - this triggers get_root_level_pending_node traversal
    gempba::node v_wrapper_l3(v_node_l3);
    bool v_result_1 = v_balancer.try_local_submit(v_wrapper_l3);

    // Push another from same level
    gempba::node v_wrapper_r3(v_node_r3);

    // WITHOUT FIX: Second call would hit virtual_root with 1 child
    // The error would be: "fw_count: 0, ph_count: 0, isVirtual: true, isDiscarded: false"
    EXPECT_NO_THROW({ bool v_result_2 = v_balancer.try_local_submit(v_wrapper_r3); });

    v_balancer.wait();

    // Verify root was properly maintained throughout
    // Virtual root should either be empty or replaced
    EXPECT_LE(v_virtual_root->get_children_count(), 1) << "Virtual root should have been corrected during tree manipulation";
}

TEST_F(quasi_horizontal_load_balancer_test, stress_test_root_correction_under_rapid_parallel_calls) {
    const auto v_balancer = new gempba::quasi_horizontal_load_balancer();

    std::atomic v_execution_count{0};
    std::function<void(std::thread::id, int, gempba::node)> v_function = [&v_execution_count](std::thread::id, int, const gempba::node&) { ++v_execution_count; };
    int v_dummy_value = 7;

    constexpr int SIZE = 5;
    // Create multiple trees to stress test the root correction logic
    std::vector<std::shared_ptr<gempba::node_core>> v_roots(SIZE);
    std::vector<std::shared_ptr<gempba::node_core>> v_left_leaves(SIZE); // these will be submitted rapidly
    std::vector<std::shared_ptr<gempba::node_core>> v_right_leaves(SIZE); // this should become the new root after correction

    // Changed: Create threads but store them for ordered joining
    std::vector<std::thread> v_threads;
    std::mutex v_mutex;

    v_threads.reserve(SIZE);
    for (int i = 0; i < SIZE; ++i) {
        v_threads.emplace_back([&, i]() { // Capture i by value to ensure correct index
            std::scoped_lock v_guard(v_mutex);
            /**
             *           root
             *          /    \
             *         left   right
             *        /   \
             *       /     \
             *   l_child   r_child
             *
             **/

            auto v_root = gempba::node_core_impl<void()>::create_dummy(*v_balancer);
            auto v_left = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_root, v_function, std::make_tuple(v_dummy_value));
            auto v_right = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_root, v_function, std::make_tuple(v_dummy_value));

            const auto v_left_child = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left, v_function, std::make_tuple(v_dummy_value));
            const auto v_right_child = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left, v_function, std::make_tuple(v_dummy_value));
            v_left->set_state(gempba::RUNNING);

            // insert at the correct index in the vectors
            v_roots[i] = v_root;
            v_left_leaves[i] = v_left_child;
            v_right_leaves[i] = v_right_child;
        });
    }

    // Changed: Join all threads in order
    for (auto& v_t: v_threads) {
        v_t.join();
    }

    // Each accepted submission dispatches two tasks, the leaf and the pending right sibling at the root level, so the pool
    // holds them all and never reports full, whatever the pace of its threads.
    v_balancer->set_thread_pool_size(2 * SIZE);
    v_balancer->wait(); // give time for threads to reach the condition_variable

    // Rapidly submit all left leaves - this should trigger multiple root corrections
    for (auto& v_leaf: v_left_leaves) {
        gempba::node v_wrapper(v_leaf);
        // WITHOUT FIX: Some of these would hit the error
        bool v_success = v_balancer->try_local_submit(v_wrapper);
        EXPECT_TRUE(v_success);
    }

    v_balancer->wait();

    // All roots should have been corrected (0 or 1 children max)
    for (const auto& v_root: v_roots) {
        EXPECT_LE(v_root->get_children_count(), 1) << "Root should have been corrected during parallel submissions";
    }

    // all right leaves should be the new roots
    for (const auto& v_leaf: v_right_leaves) {
        EXPECT_EQ(nullptr, v_leaf->get_parent());
        EXPECT_EQ(v_leaf, v_leaf->get_root());
    }

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, forward_root_node_with_no_parent_only_prunes) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node&) { v_function_called = true; };

    std::shared_ptr<gempba::node_core> v_no_parent;
    auto v_root_node = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent, v_fn, std::make_tuple(0));

    ASSERT_EQ(nullptr, v_root_node->get_parent());

    gempba::node v_node(v_root_node);
    v_balancer.forward(v_node);

    EXPECT_TRUE(v_function_called);
    EXPECT_EQ(nullptr, v_root_node->get_root());
}

TEST_F(quasi_horizontal_load_balancer_test, try_remote_submit_throws_without_scheduler) {
    gempba::quasi_horizontal_load_balancer v_balancer(nullptr);

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));

    gempba::node v_node(v_child);
    EXPECT_THROW(v_balancer.try_remote_submit(v_node, 0), std::runtime_error);
}

TEST_F(quasi_horizontal_load_balancer_test, try_remote_submit_falls_back_to_local_when_no_channel) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(2);

    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).WillOnce(testing::Return(std::nullopt));

    std::atomic<bool> v_fn_called{false};
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node&) { v_fn_called = true; };
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));

    gempba::node v_node(v_child);
    const bool v_result = v_balancer.try_remote_submit(v_node, 0);

    v_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_called);
    EXPECT_EQ(1u, v_balancer.get_spawn_remote_rejected_count());
    EXPECT_EQ(0u, v_balancer.get_spawn_rejected_pool_full_count());
}

TEST_F(quasi_horizontal_load_balancer_test, try_local_submit_when_pool_full_falls_back_to_forward) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(1);

    std::promise<void> v_started;
    std::promise<void> v_release;
    auto v_release_future = v_release.get_future().share();
    v_balancer.force_local_submit([&v_started, v_release_future]() -> std::any {
        v_started.set_value();
        v_release_future.wait();
        return {};
    });
    v_started.get_future().wait();

    bool v_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node&) { v_fn_called = true; };
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));

    gempba::node v_node(v_child);
    const bool v_result = v_balancer.try_local_submit(v_node);

    v_release.set_value();
    v_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_called);
    EXPECT_EQ(1u, v_balancer.get_spawn_rejected_pool_full_count());
    EXPECT_EQ(0u, v_balancer.get_spawn_remote_rejected_count());
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_returns_null_for_unknown_thread) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::thread v_thread([] {});
    const std::thread::id v_unknown_id = v_thread.get_id();
    v_thread.join();

    const auto v_root = v_balancer.get_root(v_unknown_id);
    EXPECT_EQ(nullptr, v_root);
}

TEST_F(quasi_horizontal_load_balancer_test, send_local_throws_when_node_is_already_consumed) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(2); // guarantee pool is ready before send() inspects it
    v_balancer.wait();

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->set_state(gempba::FORWARDED);

    gempba::node v_node(v_child);
    EXPECT_THROW(v_balancer.try_local_submit(v_node), std::runtime_error);
}

TEST_F(quasi_horizontal_load_balancer_test, send_local_discards_lazy_node_when_not_worthy) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(2); // guarantee pool is ready before send() inspects it
    v_balancer.wait();

    bool v_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node&) { v_fn_called = true; };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_balancer, v_parent, v_fn, v_initializer, v_serializer, v_deserializer);

    gempba::node v_node(v_child);
    const bool v_result = v_balancer.try_local_submit(v_node);

    EXPECT_TRUE(v_result);
    EXPECT_FALSE(v_fn_called);
    EXPECT_EQ(gempba::DISCARDED, v_child->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, send_remote_throws_when_node_is_already_consumed) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->set_state(gempba::FORWARDED);

    gempba::node v_node(v_child);
    EXPECT_THROW(v_balancer.try_remote_submit(v_node, 0), std::runtime_error);
}

TEST_F(quasi_horizontal_load_balancer_test, try_local_submit_returns_true_for_a_dispatched_node) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(2);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    for (const gempba::node_state v_state: {gempba::PUSHED, gempba::SENT_TO_ANOTHER_PROCESS, gempba::DISCARDED}) {
        std::shared_ptr<gempba::node_core> v_no_parent;
        auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent, v_fn, std::make_tuple(0));
        v_child->set_state(v_state);

        gempba::node v_node(v_child);
        EXPECT_TRUE(v_balancer.try_local_submit(v_node));
        EXPECT_EQ(v_state, v_child->get_state());
    }
    v_balancer.wait();
}

TEST_F(quasi_horizontal_load_balancer_test, try_remote_submit_returns_true_for_a_dispatched_node) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).Times(0);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    for (const gempba::node_state v_state: {gempba::PUSHED, gempba::SENT_TO_ANOTHER_PROCESS, gempba::DISCARDED}) {
        std::shared_ptr<gempba::node_core> v_no_parent;
        auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent, v_fn, std::make_tuple(0));
        v_child->set_state(v_state);

        gempba::node v_node(v_child);
        EXPECT_TRUE(v_balancer.try_remote_submit(v_node, 0));
        EXPECT_EQ(v_state, v_child->get_state());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_level_pending_node_skips_a_running_left_sibling_of_the_pending_one) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_pending = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_running = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_running, v_fn, std::make_tuple(0));
    v_running->set_state(gempba::RUNNING);

    const auto v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_grandchild);

    EXPECT_EQ(v_pending, v_top_node);
    EXPECT_EQ(nullptr, v_pending->get_parent());
    EXPECT_EQ(1, v_root->get_children_count());
    EXPECT_EQ(v_root, v_running->get_parent());
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_level_pending_node_returns_null_when_no_root_child_is_unused) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_forwarded = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_running = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_running, v_fn, std::make_tuple(0));
    v_forwarded->set_state(gempba::FORWARDED);
    v_running->set_state(gempba::RUNNING);

    EXPECT_EQ(nullptr, gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_grandchild));
    EXPECT_EQ(2, v_root->get_children_count());
}

TEST_F(quasi_horizontal_load_balancer_test, send_remote_discards_lazy_node_when_not_worthy) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });

    bool v_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node&) { v_fn_called = true; };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_balancer, v_parent, v_fn, v_initializer, v_serializer, v_deserializer);

    gempba::node v_node(v_child);
    const bool v_result = v_balancer.try_remote_submit(v_node, 0);

    EXPECT_TRUE(v_result);
    EXPECT_FALSE(v_fn_called);
    EXPECT_EQ(gempba::DISCARDED, v_child->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, send_remote_continues_after_top_level_push) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).Times(3).WillRepeatedly([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });
    EXPECT_CALL(*m_scheduler_mock, force_push(testing::_, testing::_)).Times(3).WillRepeatedly([](gempba::task_packet&&, int) { return 1u; });

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };

    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    v_child1->set_state(gempba::FORWARDED);
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_fn, std::make_tuple(0), v_serializer, v_deserializer);

    gempba::node v_node(v_grandchild);
    const bool v_result = v_balancer.try_remote_submit(v_node, 0);

    EXPECT_TRUE(v_result);
    EXPECT_EQ(gempba::SENT_TO_ANOTHER_PROCESS, v_grandchild->get_state());
    EXPECT_EQ(gempba::SENT_TO_ANOTHER_PROCESS, v_child2->get_state());
    EXPECT_EQ(gempba::SENT_TO_ANOTHER_PROCESS, v_child3->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, send_local_returns_false_on_mutex_contention) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    bool v_fn_b_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn_b = [&](std::thread::id, int, const gempba::node&) { v_fn_b_called = true; };
    std::shared_ptr<gempba::node_core> v_no_parent_b;
    auto v_child_b = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent_b, v_fn_b, std::make_tuple(0));

    std::function<void(std::thread::id, int, gempba::node)> v_fn_a = [](std::thread::id, int, const gempba::node&) {};
    std::shared_ptr<gempba::node_core> v_no_parent_a;
    auto v_child_a = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent_a, v_fn_a, std::make_tuple(0));

    std::mutex v_started_mutex;
    std::condition_variable v_started_cv;
    bool v_started = false;
    std::promise<void> v_release;
    const auto v_release_future = v_release.get_future().share();

    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        {
            std::scoped_lock v_lk(v_started_mutex);
            v_started = true;
        }
        v_started_cv.notify_one();
        v_release_future.wait();
        return std::nullopt;
    });

    gempba::node v_node_a(v_child_a);
    auto v_future_a = std::async(std::launch::async, [&] { v_balancer.try_remote_submit(v_node_a, 0); });

    {
        std::unique_lock v_lk(v_started_mutex);
        v_started_cv.wait(v_lk, [&] { return v_started; });
    }

    gempba::node v_node_b(v_child_b);
    const bool v_result = v_balancer.try_local_submit(v_node_b);

    v_release.set_value();
    v_future_a.wait();
    v_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_b_called);
}

TEST_F(quasi_horizontal_load_balancer_test, send_remote_returns_false_on_mutex_contention) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    bool v_fn_b_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn_b = [&](std::thread::id, int, const gempba::node&) { v_fn_b_called = true; };
    std::shared_ptr<gempba::node_core> v_no_parent_b;
    auto v_child_b = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent_b, v_fn_b, std::make_tuple(0));

    std::function<void(std::thread::id, int, gempba::node)> v_fn_a = [](std::thread::id, int, const gempba::node&) {};
    std::shared_ptr<gempba::node_core> v_no_parent_a;
    auto v_child_a = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent_a, v_fn_a, std::make_tuple(0));

    std::mutex v_started_mutex;
    std::condition_variable v_started_cv;
    bool v_started = false;
    std::promise<void> v_release;
    const auto v_release_future = v_release.get_future().share();

    EXPECT_CALL(*m_scheduler_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        {
            std::scoped_lock v_lk(v_started_mutex);
            v_started = true;
        }
        v_started_cv.notify_one();
        v_release_future.wait();
        return std::nullopt;
    });

    gempba::node v_node_a(v_child_a);
    auto v_future_a = std::async(std::launch::async, [&] { v_balancer.try_remote_submit(v_node_a, 0); });

    {
        std::unique_lock v_lk(v_started_mutex);
        v_started_cv.wait(v_lk, [&] { return v_started; });
    }

    gempba::node v_node_b(v_child_b);
    const bool v_result = v_balancer.try_remote_submit(v_node_b, 0);

    v_release.set_value();
    v_future_a.wait();
    v_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_b_called);
    EXPECT_EQ(1u, v_balancer.get_spawn_remote_rejected_count());
    EXPECT_EQ(0u, v_balancer.get_spawn_rejected_pool_full_count());
}

TEST_F(quasi_horizontal_load_balancer_test, try_push_root_level_node_locally_skips_consumed_root_children) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    v_balancer.set_thread_pool_size(1);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, v_fn, std::make_tuple(0));
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_child1, v_fn, std::make_tuple(0));

    v_child1->set_state(gempba::RUNNING);
    v_child2->set_state(gempba::FORWARDED);

    gempba::node v_node(v_grandchild);
    EXPECT_TRUE(v_balancer.try_push_root_level_node_locally(v_node));
    v_balancer.wait();

    EXPECT_EQ(gempba::FORWARDED, v_child2->get_state());
    EXPECT_EQ(gempba::PUSHED, v_child3->get_state());
    EXPECT_EQ(nullptr, v_child3->get_parent());
}

TEST_F(quasi_horizontal_load_balancer_test, try_push_root_level_node_locally_discards_lazy_top) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };

    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_balancer, v_root, v_fn, v_initializer, v_serializer, v_deserializer);
    auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    v_child1->set_state(gempba::FORWARDED);

    gempba::node v_node(v_grandchild);
    const bool v_result = v_balancer.try_push_root_level_node_locally(v_node);

    EXPECT_TRUE(v_result);
    EXPECT_EQ(gempba::DISCARDED, v_child2->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, try_push_root_level_node_remotely_skips_consumed_root_children) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);
    EXPECT_CALL(*m_scheduler_mock, force_push(testing::_, testing::_)).WillOnce([](gempba::task_packet&&, int) { return 1u; });

    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};

    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_fn, std::make_tuple(0), v_serializer, v_deserializer);

    v_child1->set_state(gempba::RUNNING);
    v_child2->set_state(gempba::FORWARDED);

    gempba::node v_node(v_grandchild);
    EXPECT_TRUE(v_balancer.try_push_root_level_node_remotely(v_node, 0));

    EXPECT_EQ(gempba::FORWARDED, v_child2->get_state());
    EXPECT_EQ(gempba::SENT_TO_ANOTHER_PROCESS, v_child3->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, try_push_root_level_node_remotely_discards_lazy_top) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) { FAIL(); };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet&) { return std::make_tuple(0); };

    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_balancer, v_root, v_fn, v_initializer, v_serializer, v_deserializer);
    auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_fn, std::make_tuple(0), v_serializer, v_deserializer);
    v_child1->set_state(gempba::FORWARDED);

    gempba::node v_node(v_grandchild);
    const bool v_result = v_balancer.try_push_root_level_node_remotely(v_node, 0);

    EXPECT_TRUE(v_result);
    EXPECT_EQ(gempba::DISCARDED, v_child2->get_state());
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_level_pending_node_returns_null_when_root_is_null) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));

    // Override the parent thread's root entry with a null pointer; child's m_root then
    // dereferences to nullptr while child still has a non-null parent.
    std::shared_ptr<gempba::node_core> v_null;
    v_balancer.set_root(v_parent->get_thread_id(), v_null);

    ASSERT_NE(nullptr, v_child->get_parent());
    ASSERT_EQ(nullptr, v_child->get_root());

    const auto v_top_node = gempba::quasi_horizontal_load_balancer::get_root_level_pending_node(v_child);
    EXPECT_EQ(nullptr, v_top_node);
}

TEST_F(quasi_horizontal_load_balancer_test, try_correct_root_is_a_noop_when_root_is_null) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->prune(); // clears parent and root

    ASSERT_EQ(nullptr, v_child->get_parent());
    ASSERT_EQ(nullptr, v_child->get_root());

    gempba::node v_node(v_child);
    v_balancer.try_correct_root(v_node);

    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
    EXPECT_EQ(0, v_child->get_children_count());
}

TEST_F(quasi_horizontal_load_balancer_test, try_correct_root_is_a_noop_when_node_is_root_and_leaf) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    std::shared_ptr<gempba::node_core> v_no_parent;
    auto v_root_node = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_no_parent, v_fn, std::make_tuple(0));

    ASSERT_EQ(v_root_node, v_root_node->get_root());
    ASSERT_EQ(0, v_root_node->get_children_count());

    gempba::node v_node(v_root_node);
    v_balancer.try_correct_root(v_node);

    EXPECT_EQ(v_root_node, v_root_node->get_root());
    EXPECT_EQ(nullptr, v_root_node->get_parent());
    EXPECT_EQ(0, v_root_node->get_children_count());
}

namespace {
    enum class submit_order { SUBMIT_FIRST_THEN_FORWARD_SECOND, FORWARD_FIRST_THEN_SUBMIT_SECOND, FORWARD_SECOND_THEN_SUBMIT_FIRST };

    struct run_counts {
        std::atomic<int> m_inner{0};
        std::atomic<int> m_leaf{0};
        std::atomic<int> m_exceptions{0};
    };

    /**
     * @brief Explores root -> [first, second], where one of them has two leaf children submitted with
     * try_local_submit(left); forward(right), and the root's children are handled in the given order.
     */
    void explore_two_level_tree(const submit_order p_order, const bool p_inner_is_first, run_counts& p_counts) {
        gempba::quasi_horizontal_load_balancer v_balancer;
        v_balancer.set_thread_pool_size(2);

        std::function<void(std::thread::id, int, gempba::node)> v_leaf = [&p_counts](std::thread::id, int, const gempba::node&) { ++p_counts.m_leaf; };
        std::function<void(std::thread::id, int, gempba::node)> v_inner = [&](std::thread::id, int, gempba::node p_parent) {
            ++p_counts.m_inner;
            try {
                std::shared_ptr<gempba::node_core> v_parent_core;
                if (p_parent == nullptr) {
                    v_parent_core = gempba::node_core_impl<void()>::create_dummy(v_balancer);
                } else {
                    p_parent.apply_core([&v_parent_core](std::shared_ptr<gempba::node_core>& p_core) { v_parent_core = p_core; });
                }
                gempba::node v_left(gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent_core, v_leaf, std::make_tuple(0)));
                gempba::node v_right(gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent_core, v_leaf, std::make_tuple(0)));
                v_balancer.try_local_submit(v_left);
                v_balancer.forward(v_right);
            } catch (...) {
                ++p_counts.m_exceptions;
            }
        };

        auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
        gempba::node v_first(gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, p_inner_is_first ? v_inner : v_leaf, std::make_tuple(0)));
        gempba::node v_second(gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_root, p_inner_is_first ? v_leaf : v_inner, std::make_tuple(0)));

        try {
            switch (p_order) {
                case submit_order::SUBMIT_FIRST_THEN_FORWARD_SECOND: {
                    v_balancer.try_local_submit(v_first);
                    v_balancer.forward(v_second);
                    break;
                }
                case submit_order::FORWARD_FIRST_THEN_SUBMIT_SECOND: {
                    v_balancer.forward(v_first);
                    v_balancer.try_local_submit(v_second);
                    break;
                }
                case submit_order::FORWARD_SECOND_THEN_SUBMIT_FIRST: {
                    v_balancer.forward(v_second);
                    v_balancer.try_local_submit(v_first);
                    break;
                }
            }
        } catch (...) {
            ++p_counts.m_exceptions;
        }
        v_balancer.wait();
    }

    void expect_every_node_runs_once(const submit_order p_order) {
        for (const bool v_inner_is_first: {true, false}) {
            for (int v_i = 0; v_i < 20; ++v_i) {
                run_counts v_counts;
                explore_two_level_tree(p_order, v_inner_is_first, v_counts);
                ASSERT_EQ(0, v_counts.m_exceptions.load()) << "inner_is_first " << v_inner_is_first << ", iteration " << v_i;
                ASSERT_EQ(1, v_counts.m_inner.load()) << "inner_is_first " << v_inner_is_first << ", iteration " << v_i;
                ASSERT_EQ(3, v_counts.m_leaf.load()) << "inner_is_first " << v_inner_is_first << ", iteration " << v_i;
            }
        }
    }
} // namespace

TEST_F(quasi_horizontal_load_balancer_test, every_node_runs_once_when_first_child_is_submitted_and_second_forwarded) {
    expect_every_node_runs_once(submit_order::SUBMIT_FIRST_THEN_FORWARD_SECOND);
}

TEST_F(quasi_horizontal_load_balancer_test, every_node_runs_once_when_first_child_is_forwarded_and_second_submitted) {
    expect_every_node_runs_once(submit_order::FORWARD_FIRST_THEN_SUBMIT_SECOND);
}

TEST_F(quasi_horizontal_load_balancer_test, every_node_runs_once_when_second_child_is_forwarded_and_first_submitted) {
    expect_every_node_runs_once(submit_order::FORWARD_SECOND_THEN_SUBMIT_FIRST);
}
