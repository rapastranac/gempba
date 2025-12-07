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
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gempba/node_manager.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/detail/nodes/node_core_impl.hpp>
#include <gempba/stats/stats.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>

using namespace std::chrono_literals;

/**
 * @author Andres Pastrana
 * @date 2025-08-30
 */

class scheduler_worker_mock final : public gempba::scheduler::worker {
public:
    MOCK_METHOD(void, barrier, (), (override));
    MOCK_METHOD(int, rank_me, (), ( const ,override));
    MOCK_METHOD(int, world_size, (), (const ,override));
    MOCK_METHOD(void, run, (gempba::node_manager &node_manager, (std::map<int, std::shared_ptr<gempba::serial_runnable>> p_runnables)), (override));
    MOCK_METHOD(unsigned int, force_push, (gempba::task_packet &&p_task, int p_function_id), (override));
    MOCK_METHOD(std::optional<gempba::transmission_guard>, try_open_transmission_channel, (), (override));
    MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), ( const, override));
    MOCK_METHOD(unsigned int, next_process, (), (const, override));
};


class quasi_horizontal_load_balancer_test : public ::testing::Test {
protected:
    void SetUp() override {
        m_scheduler_worker_mock = new scheduler_worker_mock();
    }

    void TearDown() override {
        delete m_scheduler_worker_mock;
    }

    scheduler_worker_mock *m_scheduler_worker_mock{};
};


TEST_F(quasi_horizontal_load_balancer_test, get_strategy) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_EQ(v_load_balancer.get_balancing_policy(), gempba::QUASI_HORIZONTAL);
};

TEST_F(quasi_horizontal_load_balancer_test, generate_unique_id) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);
    const unsigned int v_id1 = v_load_balancer.generate_unique_id();
    const unsigned int v_id2 = v_load_balancer.generate_unique_id();
    EXPECT_NE(v_id1, v_id2);
}

TEST_F(quasi_horizontal_load_balancer_test, set_thread_pool_size) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);
    v_load_balancer.set_thread_pool_size(4);
    EXPECT_NO_THROW(v_load_balancer.set_thread_pool_size(2));
}

TEST_F(quasi_horizontal_load_balancer_test, get_idle_time) {
    const gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_EQ(v_load_balancer.get_idle_time(), 0);
}

TEST_F(quasi_horizontal_load_balancer_test, wait_and_is_done) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_NO_THROW(v_load_balancer.wait());
    EXPECT_TRUE(v_load_balancer.is_done());
}


TEST_F(quasi_horizontal_load_balancer_test, set_get_root) {
    const auto v_balancer = new gempba::quasi_horizontal_load_balancer(m_scheduler_worker_mock);
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    /**
     * The following lines should not be instantiated in practice, yet this is only for testing purposes
     * every node is spawned on a different thread because there is only one root per thread
     * this is done with the sole purpose of testing the parent/child association
     */
    std::mutex v_vec_mutex;
    std::vector<std::thread::id> v_thread_ids;
    std::vector<std::shared_ptr<gempba::node_core> > v_nodes;
    std::vector<std::thread> v_threads;
    for (int i = 0; i < 3; ++i) {
        v_threads.emplace_back([&]() {
            std::scoped_lock v_lock(v_vec_mutex);

            v_thread_ids.emplace_back(std::this_thread::get_id());

            std::shared_ptr<gempba::node_core> v_parent = nullptr;
            v_nodes.emplace_back(gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value)));
        });
    }
    for (auto &v_thread: v_threads) {
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
    auto v_balancer = new gempba::quasi_horizontal_load_balancer(m_scheduler_worker_mock);

    ASSERT_EQ(1, v_balancer->generate_unique_id());
    ASSERT_EQ(2, v_balancer->generate_unique_id());
    ASSERT_EQ(3, v_balancer->generate_unique_id());
    ASSERT_EQ(4, v_balancer->generate_unique_id());

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, get_root_level_pending_node) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);

    auto v_top_node = v_balancer.get_root_level_pending_node(v_parent);
    ASSERT_EQ(nullptr, v_top_node);

    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    v_top_node = v_balancer.get_root_level_pending_node(v_child1);
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

    v_top_node = v_balancer.get_root_level_pending_node(v_grand_child1);
    ASSERT_EQ(v_child2, v_top_node);

    v_top_node = v_balancer.get_root_level_pending_node(v_grand_child1);
    ASSERT_EQ(v_child3, v_top_node);

    v_top_node = v_balancer.get_root_level_pending_node(v_grand_child1);
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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) {
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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) { v_function_called = true; };
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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) { v_function_called = true; };
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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    EXPECT_CALL(*m_scheduler_worker_mock, force_push(testing::_, testing::_)).WillOnce([](gempba::task_packet &&p_task, const int p_function_id) {
        EXPECT_EQ(0, p_function_id);
        EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
        return 999;
    });

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) {
        // This is supposed to be run remotely, but mocking doesn't include it for this test
        FAIL();
    };
    int v_dummy_value = 7;

    const std::function<gempba::task_packet(int)> v_dummy_serializer = [](int) {
        return gempba::task_packet::EMPTY;
    };
    const std::function<int(gempba::task_packet)> v_dummy_deserializer = [](gempba::task_packet) {
        return 42;
    };

    // Create multi-level structure to trigger top node finding
    auto v_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function,
                                                                                    std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function,
                                                                                          std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_root, v_dummy_function,
                                                                                          std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

    v_child1->set_state(gempba::FORWARDED); // emulates a forwarded branch

    // Create grandchild to trigger top node search
    const auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_child1, v_dummy_function,
                                                                                              std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

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
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) {
        // Simple execution that completes quickly
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Create a structure that would be prone to memory leaks
    std::vector<std::shared_ptr<gempba::node_core> > v_nodes;
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
    for (const auto &v_node: v_nodes) {
        if (v_node->get_parent() == nullptr && v_node->get_root() == nullptr) {
            v_pruned_count++;
        }
    }

    // At least some nodes should have been pruned
    EXPECT_GT(v_pruned_count, 0);
}

TEST_F(quasi_horizontal_load_balancer_test, memory_leak_prevention_through_early_pruning_remotely) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    std::mutex v_dummy_mutex;
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).Times(3).WillRepeatedly([&]() {
        std::unique_lock v_lock(v_dummy_mutex);
        gempba::transmission_guard v_guard(std::move(v_lock));
        return v_guard;
    });

    EXPECT_CALL(*m_scheduler_worker_mock, force_push(testing::_, testing::_)).WillOnce([](gempba::task_packet &&p_task, const int p_function_id) {
                EXPECT_EQ(0, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            })
            .WillOnce([](gempba::task_packet &&p_task, const int p_function_id) {
                EXPECT_EQ(1, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            })
            .WillOnce([](gempba::task_packet &&p_task, const int p_function_id) {
                EXPECT_EQ(2, p_function_id);
                EXPECT_EQ(gempba::task_packet::EMPTY, p_task);
                return 999;
            });


    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) {
        // This is supposed to be run remotely, but mocking doesn't include it for this test
        FAIL();
    };
    int v_dummy_value = 7;

    const std::function<gempba::task_packet(int)> v_dummy_serializer = [](int) {
        return gempba::task_packet::EMPTY;
    };
    const std::function<int(gempba::task_packet)> v_dummy_deserializer = [](gempba::task_packet) {
        return 42;
    };

    // Create a structure that would be prone to memory leaks
    std::vector<std::shared_ptr<gempba::node_core> > v_nodes;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);

    // Create multiple child nodes
    for (int i = 0; i < 5; ++i) {
        auto v_child = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_balancer, v_parent, v_dummy_function,
                                                                                       std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
        v_nodes.push_back(v_child);
    }

    // Submit nodes and verify they get pruned
    const size_t v_initial_children = v_parent->get_children_count();
    EXPECT_EQ(5, v_initial_children);

    // Submit several nodes
    for (size_t i = 0; i < 3 && i < v_nodes.size(); ++i) {
        gempba::node v_wrapper(v_nodes[i]);
        bool v_submitted = v_balancer.try_remote_submit(v_wrapper, i);
        EXPECT_TRUE(v_submitted);
    }

    // Wait for processing
    v_balancer.wait();

    // Verify that submitted nodes were pruned (parent/root set to nullptr)
    int v_pruned_count = 0;
    for (const auto &v_node: v_nodes) {
        if (v_node->get_parent() == nullptr && v_node->get_root() == nullptr) {
            v_pruned_count++;
        }
    }

    // At least some nodes should have been pruned
    EXPECT_GT(v_pruned_count, 0);
}

TEST_F(quasi_horizontal_load_balancer_test, missing_root_correction_after_pruning_in_send) {
    auto *v_balancer = new gempba::quasi_horizontal_load_balancer();

    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) {
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
    auto v_grandchild_A = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left_child, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_grandchild_B = gempba::node_core_impl<void(int)>::create_explicit(*v_balancer, v_left_child, v_dummy_function, std::make_tuple(v_dummy_value));

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
    ASSERT_EQ(v_virtual_root, v_grandchild_A->get_root());

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

    gempba::node v_wrapper_A(v_grandchild_A);
    bool v_submitted_A = v_balancer->try_local_submit(v_wrapper_A);

    v_balancer->wait();

    // After the submission, right_child should be pruned
    EXPECT_EQ(nullptr, v_right_child->get_parent());
    EXPECT_EQ(nullptr, v_right_child->get_root());

    // The critical check: virtual_root should now have been corrected
    // It should either have 0 children (if right_child became new root)
    // OR right_child should be the new root

    if (v_submitted_A) {
        // Check that the root was corrected
        int v_virtual_root_children = v_virtual_root->get_children_count();

        // WITHOUT FIX: This would be 1 (only left_child remains)
        // WITH FIX: This should be 0 (left_child became new root and was removed)
        EXPECT_EQ(0, v_virtual_root_children) << "Root should have been corrected after pruning left_child";

        // Verify right_grandchild became the new root
        EXPECT_EQ(gempba::PUSHED, v_right_child->get_state());
        EXPECT_EQ(gempba::FORWARDED, v_left_child->get_state());
        EXPECT_EQ(gempba::PUSHED, v_grandchild_A->get_state());
        EXPECT_EQ(gempba::UNUSED, v_grandchild_B->get_state());

        EXPECT_EQ(nullptr, v_left_child->get_parent());
        EXPECT_EQ(0, v_left_child->get_children_count());
        EXPECT_EQ(nullptr, v_grandchild_A->get_parent());
        EXPECT_EQ(nullptr, v_grandchild_B->get_parent());
        EXPECT_EQ(v_grandchild_B, v_grandchild_B->get_root());
    }

    // Now try to push grandchild_B - this is where the bug manifests
    // get_root_level_pending_node(grandchild_B) would try to access virtual_root
    // which now has < 2 children and would hit the error
    gempba::node v_wrapper_B(v_grandchild_B);

    // WITHOUT FIX: This would throw the error about virtual root with 1 child
    // WITH FIX: This should work correctly
    EXPECT_NO_THROW({
            v_balancer->try_local_submit(v_wrapper_B);
            });

    v_balancer->wait();

    delete v_balancer;
}

TEST_F(quasi_horizontal_load_balancer_test, reproduce_segfault_scenario_with_virtual_root) {
    gempba::quasi_horizontal_load_balancer v_balancer(m_scheduler_worker_mock);

    std::atomic v_execution_count{0};
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&v_execution_count](std::thread::id, int, gempba::node) {
        ++v_execution_count;
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Simulate the exact scenario from the crash log:
    // - Virtual root (dummy)
    // - Deep tree with multiple levels
    // - Rapid parallel submissions that cause root children to be pruned

    auto v_virtual_root = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_node_L1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_R1 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_virtual_root, v_dummy_function, std::make_tuple(v_dummy_value));

    // Build deeper tree under R1
    auto v_node_L2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_R1, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_R2 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_R1, v_dummy_function, std::make_tuple(v_dummy_value));

    // Even deeper
    auto v_node_L3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_L2, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node_R3 = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_node_L2, v_dummy_function, std::make_tuple(v_dummy_value));

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

    ASSERT_EQ(v_virtual_root, v_node_L3->get_root());
    ASSERT_TRUE(v_virtual_root->is_dummy());

    v_balancer.set_thread_pool_size(3);

    // Push from deep level - this triggers get_root_level_pending_node traversal
    gempba::node v_wrapper_L3(v_node_L3);
    bool v_result_1 = v_balancer.try_local_submit(v_wrapper_L3);

    // Push another from same level
    gempba::node v_wrapper_R3(v_node_R3);

    // WITHOUT FIX: Second call would hit virtual_root with 1 child
    // The error would be: "fw_count: 0, ph_count: 0, isVirtual: true, isDiscarded: false"
    EXPECT_NO_THROW({
            bool v_result_2 = v_balancer.try_local_submit(v_wrapper_R3);
            });

    v_balancer.wait();

    // Verify root was properly maintained throughout
    // Virtual root should either be empty or replaced
    EXPECT_LE(v_virtual_root->get_children_count(), 1)
        << "Virtual root should have been corrected during tree manipulation";
}

TEST_F(quasi_horizontal_load_balancer_test, stress_test_root_correction_under_rapid_parallel_calls) {
    const auto v_balancer = new gempba::quasi_horizontal_load_balancer();

    std::atomic v_execution_count{0};
    std::function<void(std::thread::id, int, gempba::node)> v_function = [&v_execution_count](std::thread::id, int, gempba::node) {
        ++v_execution_count;
    };
    int v_dummy_value = 7;

    constexpr int v_size = 5;
    // Create multiple trees to stress test the root correction logic
    std::vector<std::shared_ptr<gempba::node_core> > v_roots(v_size);
    std::vector<std::shared_ptr<gempba::node_core> > v_left_leaves(v_size); // these will be submitted rapidly
    std::vector<std::shared_ptr<gempba::node_core> > v_right_leaves(v_size); // this should become the new root after correction

    // Changed: Create threads but store them for ordered joining
    std::vector<std::thread> v_threads;
    std::mutex v_mutex;

    for (int i = 0; i < v_size; ++i) {
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

            // insert at the correct index in the vectors
            v_roots[i] = v_root;
            v_left_leaves[i] = v_left_child;
            v_right_leaves[i] = v_right_child;
        });
    }

    // Changed: Join all threads in order
    for (auto &v_t : v_threads) {
        v_t.join();
    }

    v_balancer->set_thread_pool_size(4);
    v_balancer->wait(); // give time for threads to reach the condition_variable

    // Rapidly submit all left leaves - this should trigger multiple root corrections
    for (auto &v_leaf: v_left_leaves) {
        gempba::node v_wrapper(v_leaf);
        // WITHOUT FIX: Some of these would hit the error
        bool v_success = v_balancer->try_local_submit(v_wrapper);
        EXPECT_TRUE(v_success);
    }

    v_balancer->wait();

    // All roots should have been corrected (0 or 1 children max)
    for (const auto &v_root: v_roots) {
        EXPECT_LE(v_root->get_children_count(), 1) << "Root should have been corrected during parallel submissions";
    }

    //all right leaves should be the new roots
    for (const auto &v_leaf: v_right_leaves) {
        EXPECT_EQ(nullptr, v_leaf->get_parent());
        EXPECT_EQ(v_leaf, v_leaf->get_root());
    }

    delete v_balancer;
}