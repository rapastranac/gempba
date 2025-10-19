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
#include <memory>
#include <string>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <branch_handling/branch_handler.hpp>
#include <gempba/core/node.hpp>
#include <gempba/core/node_core.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <impl/nodes/node_factory.hpp>


class node_core_mock final : public gempba::node_core {

public:
    MOCK_METHOD(void, run, (), (override));
    MOCK_METHOD(void, delegate_locally, (gempba::load_balancer * p_manager), (override));
    MOCK_METHOD(void, delegate_remotely, (gempba::scheduler::worker * p_scheduler, int p_runner_id), (override));
    MOCK_METHOD(void, set_result, (const gempba::task_packet &p_result), (override));
    MOCK_METHOD(gempba::task_packet, get_result, (), (override));
    MOCK_METHOD(gempba::task_packet, serialize, (), (override));
    MOCK_METHOD(void, deserialize, (const gempba::task_packet &p_buffer), (override));
    MOCK_METHOD(void, set_result_serializer, (const std::function<gempba::task_packet(std::any)> &p_result_serializer), (override));
    MOCK_METHOD(void, set_result_deserializer, (const std::function<std::any(gempba::task_packet)> &p_result_deserializer), (override));
    MOCK_METHOD(bool, is_dummy, (), (const override));
    MOCK_METHOD(std::thread::id, get_thread_id, (), (const override));
    MOCK_METHOD(int, get_node_id, (), (const override));
    MOCK_METHOD(int, get_forward_count, (), (const override));
    MOCK_METHOD(int, get_push_count, (), (const override));
    MOCK_METHOD(gempba::node_state, get_state, (), (const override));
    MOCK_METHOD(void, set_state, (gempba::node_state p_state), (override));
    MOCK_METHOD(bool, is_consumed, (), (const override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_root, (), (override));
    MOCK_METHOD(void, set_parent, (const std::shared_ptr<node_core> &p_parent), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_parent, (), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_leftmost_child, (), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_second_leftmost_child, (), (override));
    MOCK_METHOD(void, remove_leftmost_child, (), (override));
    MOCK_METHOD(void, remove_second_leftmost_child, (), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_leftmost_sibling, (), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_left_sibling, (), (override));
    MOCK_METHOD(std::shared_ptr<node_core>, get_right_sibling, (), (override));
    MOCK_METHOD(std::list<std::shared_ptr<node_core>>, get_children, (), (override));
    MOCK_METHOD(int, get_children_count, (), (const override));
    MOCK_METHOD(void, add_child, (const std::shared_ptr<node_core> &p_child), (override));
    MOCK_METHOD(std::any, get_any_result, (), (override));
    MOCK_METHOD(bool, is_result_ready, (), (const override));
    MOCK_METHOD(bool, should_branch, (), (override));
    MOCK_METHOD(void, remove_child, (std::shared_ptr<node_core> & p_child), (override));
    MOCK_METHOD(void, prune, (), (override));
};

class node_test : public ::testing::Test {
protected:
    void SetUp() override {
        // TODO...???
        m_node_core_mock = std::make_shared<node_core_mock>();
    }

    void TearDown() override {
        // TODO...???
    }

    std::shared_ptr<node_core_mock> m_node_core_mock;
};


TEST_F(node_test, constructor_test) { ASSERT_NO_THROW(gempba::node v_node(m_node_core_mock)); }

TEST_F(node_test, all_methods_test) {
    const auto v_this_thread_id = std::this_thread::get_id();
    std::atomic v_value(0);
    EXPECT_CALL(*m_node_core_mock, run()).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, delegate_locally(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, delegate_remotely(::testing::_, ::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, set_result(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, get_result()).WillOnce([&v_value] {
        ++v_value;
        return gempba::task_packet(std::to_string(v_value.load()));
    });
    EXPECT_CALL(*m_node_core_mock, serialize()).WillOnce([&v_value] {
        ++v_value;
        return gempba::task_packet(std::to_string(v_value.load()));
    });
    EXPECT_CALL(*m_node_core_mock, deserialize(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, set_result_serializer(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, set_result_deserializer(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, is_dummy()).WillOnce([&] {
        ++v_value;
        return true;
    });
    EXPECT_CALL(*m_node_core_mock, get_thread_id()).WillOnce([&] {
        ++v_value;
        return v_this_thread_id;
    });
    EXPECT_CALL(*m_node_core_mock, get_node_id()).WillOnce([&] { return ++v_value; });
    EXPECT_CALL(*m_node_core_mock, get_forward_count()).WillOnce([&] { return ++v_value; });
    EXPECT_CALL(*m_node_core_mock, get_push_count()).WillOnce([&] { return ++v_value; });
    EXPECT_CALL(*m_node_core_mock, get_state()).WillOnce([&] {
        ++v_value;
        return gempba::node_state::UNUSED;
    });
    EXPECT_CALL(*m_node_core_mock, set_state(::testing::_)).WillOnce([&v_value] { ++v_value; });
    EXPECT_CALL(*m_node_core_mock, is_consumed()).WillOnce([&] {
        ++v_value;
        return true;
    });
    EXPECT_CALL(*m_node_core_mock, get_root()).WillOnce([&] {
        ++v_value;
        return m_node_core_mock;
    });
    EXPECT_CALL(*m_node_core_mock, get_any_result()).WillOnce([&] {
        ++v_value;
        return std::any(42);
    });
    EXPECT_CALL(*m_node_core_mock, is_result_ready()).WillOnce([&] {
        ++v_value;
        return true;
    });
    EXPECT_CALL(*m_node_core_mock, should_branch()).WillOnce([&] {
        ++v_value;
        return true;
    });


    gempba::load_balancer *v_dummy{};
    gempba::node v_node(m_node_core_mock);
    v_node.run();

    EXPECT_EQ(1, v_value.load());

    v_node.delegate_locally(v_dummy);
    EXPECT_EQ(2, v_value.load());

    v_node.delegate_remotely(nullptr, 0);
    EXPECT_EQ(3, v_value.load());

    v_node.set_result(gempba::task_packet::EMPTY);
    EXPECT_EQ(4, v_value.load());

    const gempba::task_packet v_result = v_node.get_result();
    EXPECT_EQ(5, v_value.load());
    const std::string v_result_str(reinterpret_cast<const char *>(v_result.data()), v_result.size());
    EXPECT_EQ(v_result_str, "5");

    const gempba::task_packet v_serialized = v_node.serialize();
    EXPECT_EQ(6, v_value.load());
    const std::string v_serialized_str(reinterpret_cast<const char *>(v_serialized.data()), v_serialized.size());
    EXPECT_EQ(v_serialized_str, "6");

    v_node.deserialize(gempba::task_packet::EMPTY);
    EXPECT_EQ(7, v_value.load());

    std::function<gempba::task_packet(std::any)> v_dummy_serializer;
    v_node.set_result_serializer(v_dummy_serializer);
    EXPECT_EQ(8, v_value.load());

    std::function<std::any(gempba::task_packet)> v_dummy_deserializer;
    v_node.set_result_deserializer(v_dummy_deserializer);
    EXPECT_EQ(9, v_value.load());

    bool v_is_dummy = v_node.is_dummy();
    EXPECT_EQ(10, v_value.load());
    EXPECT_TRUE(v_is_dummy);

    const std::thread::id v_thread_id = v_node.get_thread_id();
    EXPECT_EQ(11, v_value.load());
    EXPECT_EQ(v_this_thread_id, v_thread_id);

    int v_node_id = v_node.get_node_id();
    EXPECT_EQ(12, v_value.load());
    EXPECT_EQ(12, v_node_id);

    int v_forward_count = v_node.get_forward_count();
    EXPECT_EQ(13, v_value.load());
    EXPECT_EQ(13, v_forward_count);

    int v_push_count = v_node.get_push_count();
    EXPECT_EQ(14, v_value.load());
    EXPECT_EQ(14, v_push_count);

    const gempba::node_state v_node_state = v_node.get_state();
    EXPECT_EQ(15, v_value.load());
    EXPECT_EQ(gempba::UNUSED, v_node_state);

    v_node.set_state(gempba::UNUSED);
    EXPECT_EQ(16, v_value.load());

    bool v_is_consumed = v_node.is_consumed();
    EXPECT_EQ(17, v_value.load());
    EXPECT_TRUE(v_is_consumed);

    // const gempba::node v_dummy_node;
    // v_node.set_root(v_dummy_node);
    EXPECT_EQ(17, v_value.load());

    const gempba::node v_root = v_node.get_root();
    EXPECT_EQ(18, v_value.load());
    EXPECT_EQ(v_root, v_node);

    std::any v_any_result = v_node.get_any_result();
    EXPECT_EQ(19, v_value.load());
    const int v_typed_result = std::any_cast<int>(v_any_result);
    EXPECT_EQ(42, v_typed_result);

    bool v_is_result_ready = v_node.is_result_ready();
    EXPECT_EQ(20, v_value.load());
    EXPECT_TRUE(v_is_result_ready);

    bool v_should_branch = v_node.should_branch();
    EXPECT_EQ(21, v_value.load());
    EXPECT_TRUE(v_should_branch);
}

// Test fixture for node operator tests

class node_operator_test : public ::testing::Test {
protected:
    void SetUp() override {
        // Create shared pointers to mock objects
        m_mock_core1 = std::make_shared<node_core_mock>();
        m_mock_core2 = std::make_shared<node_core_mock>();

        // Create nodes with different cores
        m_node1 = gempba::node(m_mock_core1);
        m_node2 = gempba::node(m_mock_core2);
        m_node3 = gempba::node(m_mock_core1); // Same core as node1
        m_null_node = gempba::node(); // Default constructed (null)
    }

    std::shared_ptr<node_core_mock> m_mock_core1;
    std::shared_ptr<node_core_mock> m_mock_core2;
    gempba::node m_node1;
    gempba::node m_node2;
    gempba::node m_node3;
    gempba::node m_null_node;
};

TEST_F(node_operator_test, equality_operator) {
    // Same core should be equal
    EXPECT_TRUE(m_node1 == m_node3);

    // Different cores should not be equal
    EXPECT_FALSE(m_node1 == m_node2);
    EXPECT_FALSE(m_node2 == m_node3);

    // Null nodes should be equal
    const gempba::node v_another_null_node;
    EXPECT_TRUE(m_null_node == v_another_null_node);

    // Non-null vs null should not be equal
    EXPECT_FALSE(m_node1 == m_null_node);
    EXPECT_FALSE(m_null_node == m_node1);
}

TEST_F(node_operator_test, copy_constructor) {
    const gempba::node v_copied_node(m_node1);

    // Copied node should be equal to original
    EXPECT_TRUE(v_copied_node == m_node1);

    // Copy of null node
    const gempba::node v_copied_null(m_null_node);
    EXPECT_TRUE(v_copied_null == m_null_node);
}

TEST_F(node_operator_test, copy_assignment_operator) {
    gempba::node v_assigned_node;
    v_assigned_node = m_node1;

    // Assigned node should be equal to original
    EXPECT_TRUE(v_assigned_node == m_node1);

    // Self assignment
    v_assigned_node = v_assigned_node;
    EXPECT_TRUE(v_assigned_node == m_node1);

    // Assignment from null
    v_assigned_node = m_null_node;
    EXPECT_TRUE(v_assigned_node == m_null_node);
}

TEST_F(node_operator_test, move_constructor) {
    // Create a copy to move from
    gempba::node v_to_be_moved(m_node1);
    const gempba::node v_original_copy(m_node1);

    // Move construct
    const gempba::node v_moved_node(std::move(v_to_be_moved));

    // Moved node should equal original
    EXPECT_TRUE(v_moved_node == v_original_copy);

    // Move from null
    gempba::node v_null_to_move;
    const gempba::node v_moved_null(std::move(v_null_to_move));
    EXPECT_TRUE(v_moved_null == m_null_node);
}

TEST_F(node_operator_test, move_assignment_operator) {
    // Create a copy to move from
    gempba::node v_to_be_moved(m_node1);
    const gempba::node v_original_copy(m_node1);

    gempba::node v_move_assigned_node;
    v_move_assigned_node = std::move(v_to_be_moved);

    // Move assigned node should equal original
    EXPECT_TRUE(v_move_assigned_node == v_original_copy);

    // Self move assignment (should be safe)
    v_move_assigned_node = std::move(v_move_assigned_node);
    EXPECT_TRUE(v_move_assigned_node == v_original_copy);
}

TEST_F(node_operator_test, bool_conversion_operator) {
    // Non-null node should be true
    EXPECT_TRUE(static_cast<bool>(m_node1));
    EXPECT_TRUE(static_cast<bool>(m_node2));

    // Null node should be false
    EXPECT_FALSE(static_cast<bool>(m_null_node));

    // Test in conditional contexts
    if (m_node1) {
        SUCCEED();
    } else {
        FAIL() << "Non-null node should evaluate to true";
    }

    if (!m_null_node) {
        SUCCEED();
    } else {
        FAIL() << "Null node should evaluate to false";
    }
}

TEST_F(node_operator_test, equality_with_nullptr_member) {
    // Null node should equal nullptr
    EXPECT_TRUE(m_null_node == nullptr);

    // Non-null node should not equal nullptr
    EXPECT_FALSE(m_node1 == nullptr);
    EXPECT_FALSE(m_node2 == nullptr);
}

TEST_F(node_operator_test, inequality_with_nullptr_member) {
    // Null node should not be unequal to nullptr
    EXPECT_FALSE(m_null_node != nullptr);

    // Non-null node should be unequal to nullptr
    EXPECT_TRUE(m_node1 != nullptr);
    EXPECT_TRUE(m_node2 != nullptr);
}

TEST_F(node_operator_test, non_member_equality_with_nullptr) {
    // nullptr should equal null node
    EXPECT_TRUE(nullptr == m_null_node);

    // nullptr should not equal non-null node
    EXPECT_FALSE(nullptr == m_node1);
    EXPECT_FALSE(nullptr == m_node2);
}

TEST_F(node_operator_test, non_member_inequality_with_nullptr) {
    // nullptr should not be unequal to null node
    EXPECT_FALSE(nullptr != m_null_node);

    // nullptr should be unequal to non-null node
    EXPECT_TRUE(nullptr != m_node1);
    EXPECT_TRUE(nullptr != m_node2);
}

TEST_F(node_operator_test, operator_consistency) {
    // Test that == and != are consistent
    EXPECT_EQ(m_node1 == m_node2, !(m_node1 != m_node2));
    EXPECT_EQ(m_node1 == m_node3, !(m_node1 != m_node3));

    // Test that bool conversion and nullptr comparison are consistent
    EXPECT_EQ(static_cast<bool>(m_node1), m_node1 != nullptr);
    EXPECT_EQ(static_cast<bool>(m_null_node), m_null_node != nullptr);

    // Test symmetry of nullptr operators
    EXPECT_EQ(m_node1 == nullptr, nullptr == m_node1);
    EXPECT_EQ(m_node1 != nullptr, nullptr != m_node1);
    EXPECT_EQ(m_null_node == nullptr, nullptr == m_null_node);
    EXPECT_EQ(m_null_node != nullptr, nullptr != m_null_node);
}

TEST_F(node_operator_test, edge_cases) {
    // Multiple null nodes should all be equal
    const gempba::node v_null1;
    const gempba::node v_null2;
    const gempba::node v_null3;

    EXPECT_TRUE(v_null1 == v_null2);
    EXPECT_TRUE(v_null2 == v_null3);
    EXPECT_TRUE(v_null1 == v_null3);

    // All should equal nullptr
    EXPECT_TRUE(v_null1 == nullptr);
    EXPECT_TRUE(v_null2 == nullptr);
    EXPECT_TRUE(v_null3 == nullptr);

    // Chain of assignments
    gempba::node v_chain1, v_chain2, v_chain3;
    v_chain1 = m_node1;
    v_chain2 = v_chain1;
    v_chain3 = v_chain2;

    EXPECT_TRUE(v_chain1 == v_chain2);
    EXPECT_TRUE(v_chain2 == v_chain3);
    EXPECT_TRUE(v_chain1 == v_chain3);
    EXPECT_TRUE(v_chain3 == m_node1);
}


/**
 * @attention this is a very special test, the node instance could die before it gets executed by the internal
 * thread pool in the node manager, so we need to mimic a short lifespan of a node
 */
TEST_F(node_test, delegate_locally_test) {
    auto v_load_balancer = gempba::quasi_horizontal_load_balancer(nullptr);
    v_load_balancer.set_thread_pool_size(1); // fewer threads on the pipeline runner (GitHub)
    v_load_balancer.wait(); // it guarantees that the threads in the threadpool reach the condition_variable (GitHub runner limitation)
    const gempba::branch_handler v_manager(&v_load_balancer, nullptr);

    int v_value = 0;

    {
        auto v_dummy_function = [&v_value](std::thread::id, const int p_int, const double p_double, gempba::node p_parent) {
            ASSERT_EQ(7, p_int);
            ASSERT_EQ(6.0, p_double);
            ASSERT_TRUE(p_parent == nullptr);
            v_value = p_int * static_cast<int>(p_double);
        };

        const std::function<std::optional<std::tuple<int, double> >()> v_args_initializer = [] {
            return std::make_tuple(7, 6.0);
        };
        auto v_explicit_node = gempba::node_factory::create_lazy_node<void>(v_load_balancer, gempba::node(nullptr), v_dummy_function, v_args_initializer);

        const bool v_submitted = v_manager.try_local_submit(v_explicit_node);
        ASSERT_TRUE(v_submitted);
    }

    v_manager.wait();

    ASSERT_EQ(42, v_value); // 7 * 6 = 42
}
