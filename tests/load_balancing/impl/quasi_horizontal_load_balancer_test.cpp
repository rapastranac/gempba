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

#include <branch_handling/branch_handler.hpp>
#include <gempba/detail/nodes/node_core_impl.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <runnables/api/serial_runnable.hpp>
#include <schedulers/api/scheduler.hpp>
#include <schedulers/api/stats.hpp>

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
    MOCK_METHOD(void, run, (gempba::branch_handler &branch_handler, (std::map<int, std::shared_ptr<gempba::serial_runnable>> p_runnables)), (override));
    MOCK_METHOD(unsigned int, force_push, (gempba::task_packet &&p_task, int p_function_id), (override));
    MOCK_METHOD(std::optional<gempba::transmission_guard>, try_open_transmission_channel, (), (override));
    MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), ( const, override));
    MOCK_METHOD(unsigned int, next_process, (), (const, override));
};


class quasi_horizontal_load_balancer_test : public ::testing::Test {
protected:
    void SetUp() override {
        m_scheduler_worker_mock = new scheduler_worker_mock();
        m_balancer = new gempba::quasi_horizontal_load_balancer(m_scheduler_worker_mock);
    }

    void TearDown() override {
        delete m_scheduler_worker_mock;
    }

    scheduler_worker_mock *m_scheduler_worker_mock{};
    gempba::quasi_horizontal_load_balancer *m_balancer{};
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
            v_nodes.emplace_back(gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value)));
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


    m_balancer->set_root(v_tid_1, v_root1);
    m_balancer->set_root(v_tid_2, v_root2);
    m_balancer->set_root(v_tid_3, v_root3);

    const auto v_actual_root1 = m_balancer->get_root(v_tid_1);
    const auto v_actual_root2 = m_balancer->get_root(v_tid_2);
    const auto v_actual_root3 = m_balancer->get_root(v_tid_3);

    ASSERT_EQ(v_root1, *v_actual_root1);
    ASSERT_EQ(v_root2, *v_actual_root2);
    ASSERT_EQ(v_root3, *v_actual_root3);
}

TEST_F(quasi_horizontal_load_balancer_test, prune) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_EQ(v_parent, v_child1->get_parent());
    ASSERT_EQ(v_parent, v_child2->get_parent());
    ASSERT_EQ(v_parent, v_child3->get_parent());

    ASSERT_EQ(v_parent, v_child1->get_root());
    ASSERT_EQ(v_parent, v_child2->get_root());
    ASSERT_EQ(v_parent, v_child3->get_root());


    m_balancer->prune(v_child1);
    ASSERT_EQ(nullptr, v_child1->get_parent());
    ASSERT_EQ(nullptr, v_child1->get_root());

    m_balancer->prune(v_child2);
    ASSERT_EQ(nullptr, v_child2->get_parent());
    ASSERT_EQ(nullptr, v_child2->get_root());

    m_balancer->prune(v_child3);
    ASSERT_EQ(nullptr, v_child3->get_parent());
    ASSERT_EQ(nullptr, v_child3->get_root());
}

TEST_F(quasi_horizontal_load_balancer_test, get_unique_id) {
    ASSERT_EQ(1, m_balancer->generate_unique_id());
    ASSERT_EQ(2, m_balancer->generate_unique_id());
    ASSERT_EQ(3, m_balancer->generate_unique_id());
    ASSERT_EQ(4, m_balancer->generate_unique_id());
}

TEST_F(quasi_horizontal_load_balancer_test, prune_left_sibling) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    // create a dummy parent
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_EQ(3, v_parent->get_children_count());

    m_balancer->prune_left_sibling(v_child3);

    ASSERT_EQ(2, v_parent->get_children_count());
    // child1 should be pruned
    ASSERT_EQ(nullptr, v_child1->get_parent());
    ASSERT_EQ(nullptr, v_child1->get_root());

    m_balancer->prune_left_sibling(v_child3);

    // As child3 is the only remaining child, it becomes the new root
    // therefore the parent should be pruned
    ASSERT_EQ(0, v_parent->get_children_count());
    ASSERT_EQ(nullptr, v_parent->get_parent());
    ASSERT_EQ(nullptr, v_parent->get_root());
    // child2 should be pruned
    ASSERT_EQ(nullptr, v_child2->get_parent());
    ASSERT_EQ(nullptr, v_child2->get_root());

    // child3 should be the new root
    ASSERT_EQ(v_child3, v_child3->get_root());
    ASSERT_EQ(nullptr, v_child3->get_parent());

    m_balancer->prune_left_sibling(v_child3); // exits
}

TEST_F(quasi_horizontal_load_balancer_test, find_top_node) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);

    auto v_top_node = m_balancer->find_top_node(v_parent);
    ASSERT_EQ(nullptr, v_top_node);

    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    v_top_node = m_balancer->find_top_node(v_child1);
    ASSERT_EQ(nullptr, v_top_node);


    /**
     * From this point onwards:
     * @code
     *           root != dummy
     *            |  \  \   \
     *            |   \    \    \
     *            |    \      \     \
     *            |     \        \      \
     *         child<sub>1</sub>  child<sub>2</sub>  child<sub>3</sub>  child<sub>4</sub>
     *            |
     *       grandChild<sub>1</sub>    <-- this level
     *
     * @endcode
     *
     */

    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child4 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    auto v_grand_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));

    v_top_node = m_balancer->find_top_node(v_grand_child1);
    ASSERT_EQ(v_child2, v_top_node);

    v_top_node = m_balancer->find_top_node(v_grand_child1);
    ASSERT_EQ(v_child3, v_top_node);

    v_top_node = m_balancer->find_top_node(v_grand_child1);
    ASSERT_EQ(v_child4, v_top_node);

    // grandChild1 should become the new root
    ASSERT_EQ(v_grand_child1, v_grand_child1->get_root());
    ASSERT_EQ(nullptr, v_grand_child1->get_parent());
}

TEST_F(quasi_horizontal_load_balancer_test, maybe_prune_left_sibling_first_level) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void(int)>::create_dummy(*m_balancer);

    // first level
    auto v_node11 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node12 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node13 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node14 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));


    /**
     *  @code
     *                dummy
     *             /   | \   \
     *           /     |   \    \
     *          /      |      \     \
     *         /       |        \     \
     *        node<sub>1,1</sub>  node<sub>1,2</sub> node <sub>1,3</sub> node<sub>1,4</sub>
     *
     * @endcode
     */

    {
        // nothing changes, because child1 is the first child
        m_balancer->maybe_prune_left_sibling(v_node11);
        auto v_children = v_parent->get_children();
        ASSERT_EQ(4, v_parent->get_children_count());
        auto it = v_children.begin();
        ASSERT_EQ(v_node11, *it);
        ASSERT_EQ(v_node12, *(++it));
        ASSERT_EQ(v_node13, *(++it));
        ASSERT_EQ(v_node14, *(++it));
    }

    {
        m_balancer->maybe_prune_left_sibling(v_node12);
        auto v_children = v_parent->get_children();
        ASSERT_EQ(nullptr, v_node11->get_parent());
        ASSERT_EQ(nullptr, v_node11->get_root());

        ASSERT_EQ(3, v_parent->get_children_count());
        auto it = v_children.begin();
        ASSERT_EQ(v_node12, *it);
        ASSERT_EQ(v_node13, *(++it));
        ASSERT_EQ(v_node14, *(++it));
    }

    {
        m_balancer->maybe_prune_left_sibling(v_node13);
        auto v_children = v_parent->get_children();
        ASSERT_EQ(nullptr, v_node12->get_parent());
        ASSERT_EQ(nullptr, v_node12->get_root());

        ASSERT_EQ(2, v_parent->get_children_count());
        auto it = v_children.begin();
        ASSERT_EQ(v_node13, *it);
        ASSERT_EQ(v_node14, *(++it));
    }

    {
        // here node14 becomes the new root as it is the last child
        m_balancer->maybe_prune_left_sibling(v_node14);
        ASSERT_EQ(nullptr, v_node13->get_parent());
        ASSERT_EQ(nullptr, v_node13->get_root());

        ASSERT_EQ(0, v_parent->get_children_count());
        ASSERT_EQ(nullptr, v_node14->get_parent());
        ASSERT_EQ(v_node14, v_node14->get_root());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, maybe_prune_left_sibling_other_level) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void(int)>::create_dummy(*m_balancer);

    // first level
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    // second level
    auto v_child1_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child1_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));

    // third level
    auto v_child1_child1_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1_child1, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child1_child1_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1_child1, v_dummy_function, std::make_tuple(v_dummy_value));

    {
        // nothing changes, because child1Child1Child1 is the first child
        m_balancer->maybe_prune_left_sibling(v_child1_child1_child1);
        auto v_children = v_child1_child1->get_children();
        ASSERT_EQ(2, v_child1_child1->get_children_count());
        auto it = v_children.begin();
        ASSERT_EQ(v_child1_child1_child1, *it);
        ASSERT_EQ(v_child1_child1_child2, *(++it));
    }

    {
        m_balancer->maybe_prune_left_sibling(v_child1_child1_child2);
        ASSERT_EQ(1, v_child1_child1->get_children_count());
        ASSERT_EQ(v_child1_child1_child2, v_child1_child1->get_leftmost_child());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, maybe_prune_left_sibling_other_level_lowering_root_1) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void(int)>::create_dummy(*m_balancer);

    // first level
    auto v_node11 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node12 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    // second level
    auto v_node21 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node12, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node22 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node12, v_dummy_function, std::make_tuple(v_dummy_value));

    // third level
    auto v_node31 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node21, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node32 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node21, v_dummy_function, std::make_tuple(v_dummy_value));

    // assert that the root of all nodes is dummy
    ASSERT_EQ(v_parent, v_node11->get_root());
    ASSERT_EQ(v_parent, v_node12->get_root());
    ASSERT_EQ(v_parent, v_node21->get_root());
    ASSERT_EQ(v_parent, v_node22->get_root());
    ASSERT_EQ(v_parent, v_node31->get_root());
    ASSERT_EQ(v_parent, v_node32->get_root());


    /**
     * Given the following structure:
     * @code
     *          dummy
     *          /  |
     *     node<sub>1,1</sub>  node<sub>1,2</sub>
     *             |    \
     *           node<sub>2,1</sub>  node<sub>2,2</sub>
     *           /    \
     *        node<sub>3,1</sub>  node<sub>3,2</sub>
     *
     * @endcode
     *
     * When we call <code>maybe_prune_left_sibling(node12)</code> then, <code>node<sub>1,2</sub></code> ends up without siblings, therefore
     * it should become the new root.
     */

    m_balancer->maybe_prune_left_sibling(v_node12);
    ASSERT_EQ(nullptr, v_parent->get_parent());
    ASSERT_EQ(nullptr, v_parent->get_root());
    ASSERT_EQ(0, v_parent->get_children_count());

    ASSERT_EQ(nullptr, v_node11->get_parent());
    ASSERT_EQ(nullptr, v_node11->get_root());

    // assert that the root of all nodes is node12
    ASSERT_EQ(v_node12, v_node12->get_root());
    ASSERT_EQ(v_node12, v_node21->get_root());
    ASSERT_EQ(v_node12, v_node22->get_root());
    ASSERT_EQ(v_node12, v_node31->get_root());
    ASSERT_EQ(v_node12, v_node32->get_root());
}

TEST_F(quasi_horizontal_load_balancer_test, maybe_prune_left_sibling_other_level_lowering_root_2) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) { FAIL(); };
    int v_dummy_value = 7;

    auto v_dummy = gempba::node_core_impl<void(int)>::create_dummy(*m_balancer);

    // first level
    auto v_node11 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_dummy, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node12 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_dummy, v_dummy_function, std::make_tuple(v_dummy_value));

    // second level
    auto v_node21 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node12, v_dummy_function, std::make_tuple(v_dummy_value));

    // third level
    auto v_node31 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node21, v_dummy_function, std::make_tuple(v_dummy_value));

    // fourth level
    auto v_node41 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node31, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_node42 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_node31, v_dummy_function, std::make_tuple(v_dummy_value));


    // assert that the root of all nodes is dummy
    ASSERT_EQ(v_dummy, v_node11->get_root());
    ASSERT_EQ(v_dummy, v_node12->get_root());
    ASSERT_EQ(v_dummy, v_node21->get_root());
    ASSERT_EQ(v_dummy, v_node31->get_root());
    ASSERT_EQ(v_dummy, v_node41->get_root());
    ASSERT_EQ(v_dummy, v_node42->get_root());


    /**
     * Given the following structure:
     * @code
     *          dummy
     *          /  |
     *     node<sub>1,1</sub>  node<sub>1,2</sub>
     *             |
     *           node<sub>2,1</sub>
     *             |
     *           node<sub>3,1</sub>
     *           /    \
     *        node<sub>4,1</sub>  node<sub>4,2</sub>
     *
     * @endcode
     *
     * When we call <code>maybe_prune_left_sibling(node12)</code> then, <code>node<sub>1,2</sub></code> ends up without siblings, therefore
     * a new root needs to be found, which is <code>node<sub>3,1</sub></code>, because it is first node front top-to-bottom that has at least two children
     */

    m_balancer->maybe_prune_left_sibling(v_node12);
    ASSERT_EQ(nullptr, v_dummy->get_parent());
    ASSERT_EQ(nullptr, v_dummy->get_root());
    ASSERT_EQ(0, v_dummy->get_children_count());

    ASSERT_EQ(nullptr, v_node11->get_parent());
    ASSERT_EQ(nullptr, v_node11->get_root());
    ASSERT_EQ(0, v_node11->get_children_count());

    ASSERT_EQ(nullptr, v_node12->get_parent());
    ASSERT_EQ(nullptr, v_node12->get_root());
    ASSERT_EQ(0, v_node12->get_children_count());

    ASSERT_EQ(nullptr, v_node21->get_parent());
    ASSERT_EQ(nullptr, v_node21->get_root());
    ASSERT_EQ(0, v_node21->get_children_count());

    // assert that the root of all nodes is node31
    ASSERT_EQ(nullptr, v_node31->get_parent());
    ASSERT_EQ(v_node31, v_node31->get_root());
    ASSERT_EQ(v_node31, v_node41->get_root());
    ASSERT_EQ(v_node31, v_node42->get_root());
}

TEST_F(quasi_horizontal_load_balancer_test, thread_request_count_tracking) {
    gempba::quasi_horizontal_load_balancer v_load_balancer(m_scheduler_worker_mock);

    EXPECT_EQ(0, v_load_balancer.get_thread_request_count());

    // Submit tasks and verify counter increments
    std::function<std::any()> v_dummy_task = []() -> std::any { return 42; };

    auto v_unused_future1 = v_load_balancer.force_local_submit(std::move(v_dummy_task));
    EXPECT_EQ(1, v_load_balancer.get_thread_request_count());

    auto v_unused_future2 = v_load_balancer.force_local_submit([]() -> std::any { return 24; });
    EXPECT_EQ(2, v_load_balancer.get_thread_request_count());

    v_load_balancer.wait();
}

TEST_F(quasi_horizontal_load_balancer_test, forward_with_root_correction) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) {
        // Simple function that does nothing but allows node execution
    };
    int v_dummy_value = 7;

    // Create a structure where parent == root with multiple children
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_EQ(3, v_parent->get_children_count());
    ASSERT_EQ(v_parent, v_child1->get_parent());
    ASSERT_EQ(v_parent, v_child1->get_root());

    // Forward child1 - this should NOT correct the root
    gempba::node v_node_wrapper(v_child1);
    m_balancer->forward(v_node_wrapper);

    // After forward, child1 should be pruned
    EXPECT_EQ(nullptr, v_child1->get_parent());
    EXPECT_EQ(nullptr, v_child1->get_root());

    // Parent should have fewer children
    EXPECT_EQ(v_parent->get_children_count(), 2);

    // Forward child2 - this SHOULD correct the root
    gempba::node v_node_wrapper2(v_child2);
    m_balancer->forward(v_node_wrapper2);

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
    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) { v_function_called = true; };
    int v_dummy_value = 7;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    const auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    const auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));

    // Set up thread pool with available slots
    m_balancer->set_thread_pool_size(2);

    ASSERT_EQ(v_parent, v_child1->get_parent());
    ASSERT_EQ(v_parent, v_child1->get_root());
    ASSERT_EQ(2, v_parent->get_children_count());

    // Create node wrapper and attempt local submit
    gempba::node v_node_wrapper(v_child1);

    // This should prune the node before delegation
    const bool v_submitted = m_balancer->try_local_submit(v_node_wrapper);

    // Wait for any pending operations
    m_balancer->wait();

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
    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [&](std::thread::id, int, gempba::node) { v_function_called = true; };
    int v_dummy_value = 7;

    // Create multi-level structure to trigger top node finding
    auto v_root = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child2 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));
    auto v_child3 = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_root, v_dummy_function, std::make_tuple(v_dummy_value));

    v_child1->set_state(gempba::FORWARDED); // emulates a forwarded branch

    // Create grandchild to trigger top node search
    const auto v_grandchild = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_child1, v_dummy_function, std::make_tuple(v_dummy_value));

    m_balancer->set_thread_pool_size(2);

    ASSERT_EQ(3, v_root->get_children_count());
    ASSERT_EQ(v_root, v_grandchild->get_root());

    // This should find a top node (child2) and prune it before delegation
    gempba::node v_node_wrapper(v_grandchild);
    const bool v_result = m_balancer->try_push_root_level_node_locally(v_node_wrapper);

    m_balancer->wait();

    if (v_result) {
        ASSERT_TRUE(v_function_called);
        // Verify that the found top node was properly pruned
        EXPECT_EQ(nullptr, v_child2->get_parent());
        EXPECT_EQ(nullptr, v_child2->get_root());

        EXPECT_EQ(2, v_root->get_children_count());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, top_node_pruning_in_push_operations_remotely) {

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
    auto v_root = gempba::node_core_impl<void()>::create_dummy(*m_balancer);
    auto v_child1 = gempba::node_core_impl<void(int)>::create_serializable_explicit(*m_balancer, v_root, v_dummy_function,
                                                                                    std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child2 = gempba::node_core_impl<void(int)>::create_serializable_explicit(*m_balancer, v_root, v_dummy_function,
                                                                                          std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
    const auto v_child3 = gempba::node_core_impl<void(int)>::create_serializable_explicit(*m_balancer, v_root, v_dummy_function,
                                                                                          std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

    v_child1->set_state(gempba::FORWARDED); // emulates a forwarded branch

    // Create grandchild to trigger top node search
    const auto v_grandchild = gempba::node_core_impl<void(int)>::create_serializable_explicit(*m_balancer, v_child1, v_dummy_function,
                                                                                              std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);

    m_balancer->set_thread_pool_size(2);

    ASSERT_EQ(3, v_root->get_children_count());
    ASSERT_EQ(v_root, v_grandchild->get_root());

    // This should find a top node (child2) and prune it before delegation
    gempba::node v_node_wrapper(v_grandchild);
    const bool v_result = m_balancer->try_push_root_level_node_remotely(v_node_wrapper, 0);

    m_balancer->wait();

    if (v_result) {
        // Verify that the found top node was properly pruned
        EXPECT_EQ(nullptr, v_child2->get_parent());
        EXPECT_EQ(nullptr, v_child2->get_root());

        EXPECT_EQ(2, v_root->get_children_count());
    }
}

TEST_F(quasi_horizontal_load_balancer_test, memory_leak_prevention_through_early_pruning_locally) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int, gempba::node) {
        // Simple execution that completes quickly
        std::this_thread::sleep_for(1ms);
    };
    int v_dummy_value = 7;

    // Create a structure that would be prone to memory leaks
    std::vector<std::shared_ptr<gempba::node_core> > v_nodes;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);

    // Create multiple child nodes
    for (int i = 0; i < 5; ++i) {
        auto v_child = gempba::node_core_impl<void(int)>::create_explicit(*m_balancer, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
        v_nodes.push_back(v_child);
    }

    // Set up thread pool with available slots
    m_balancer->set_thread_pool_size(3);
    m_balancer->wait(); // it gives time for threads to reach the condition_variable

    // Submit nodes and verify they get pruned
    const size_t v_initial_children = v_parent->get_children_count();
    EXPECT_EQ(5, v_initial_children);

    // Submit several nodes
    for (size_t i = 0; i < 3 && i < v_nodes.size(); ++i) {
        gempba::node v_wrapper(v_nodes[i]);
        bool v_submitted = m_balancer->try_local_submit(v_wrapper);
        EXPECT_TRUE(v_submitted) << "Failed at index: " << i;
    }

    // Wait for processing
    m_balancer->wait();

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
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(*m_balancer);

    // Create multiple child nodes
    for (int i = 0; i < 5; ++i) {
        auto v_child = gempba::node_core_impl<void(int)>::create_serializable_explicit(*m_balancer, v_parent, v_dummy_function,
                                                                                       std::make_tuple(v_dummy_value), v_dummy_serializer, v_dummy_deserializer);
        v_nodes.push_back(v_child);
    }

    // Submit nodes and verify they get pruned
    const size_t v_initial_children = v_parent->get_children_count();
    EXPECT_EQ(5, v_initial_children);

    // Submit several nodes
    for (size_t i = 0; i < 3 && i < v_nodes.size(); ++i) {
        gempba::node v_wrapper(v_nodes[i]);
        bool v_submitted = m_balancer->try_remote_submit(v_wrapper, i);
        EXPECT_TRUE(v_submitted);
    }

    // Wait for processing
    m_balancer->wait();

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
