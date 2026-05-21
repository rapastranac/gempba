/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
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
#include <any>
#include <climits>
#include <functional>
#include <future>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>

#include <gempba/core/load_balancer.hpp>
#include <gempba/core/scheduler.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/detail/nodes/node_core_impl.hpp>
#include <gempba/node_manager.hpp>
#include <gempba/stats/stats.hpp>
#include <gempba/utils/score.hpp>
#include <gempba/utils/task_packet.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>
#include <map>

/**
 * @author Andrés Pastrana
 * @date 2026
 */
class load_balancer_mock_nm : public gempba::load_balancer {
public:
    MOCK_METHOD(gempba::balancing_policy, get_balancing_policy, (), (override));
    MOCK_METHOD(unsigned int, generate_unique_id, (), (override));
    MOCK_METHOD(double, get_idle_time, (), (const override));
    MOCK_METHOD(void, set_root, (std::thread::id, std::shared_ptr<gempba::node_core>&), (override));
    MOCK_METHOD(std::shared_ptr<std::shared_ptr<gempba::node_core>>, get_root, (std::thread::id), (override));
    MOCK_METHOD(void, set_thread_pool_size, (unsigned int), (override));
    MOCK_METHOD(unsigned int, get_thread_pool_size, (), (const, override));
    MOCK_METHOD(std::size_t, get_tasks_running_count, (), (const, override));
    MOCK_METHOD(std::future<std::any>, force_local_submit, (std::function<std::any()>&&), (override));
    MOCK_METHOD(void, forward, (gempba::node&), (override));
    MOCK_METHOD(bool, try_local_submit, (gempba::node&), (override));
    MOCK_METHOD(bool, try_remote_submit, (gempba::node&, int), (override));
    MOCK_METHOD(void, wait, (), (override));
    MOCK_METHOD(bool, is_done, (), (const, override));
    MOCK_METHOD(std::size_t, get_thread_request_count, (), (const, override));
};

class scheduler_worker_mock_nm : public gempba::scheduler::worker {
public:
    MOCK_METHOD(void, barrier, (), (override));
    MOCK_METHOD(int, rank_me, (), (const, override));
    MOCK_METHOD(int, world_size, (), (const, override));
    MOCK_METHOD(void, run, (gempba::node_manager & p_node_manager, (std::map<int, std::shared_ptr<gempba::serial_runnable>> p_runnables)), (override));
    MOCK_METHOD(unsigned int, force_push, (gempba::task_packet && p_task, int p_function_id), (override));
    MOCK_METHOD(std::optional<gempba::transmission_guard>, try_open_transmission_channel, (), (override));
    MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), (const, override));
    MOCK_METHOD(unsigned int, next_process, (), (const, override));
};


class node_manager_test : public ::testing::Test {
protected:
    testing::NiceMock<load_balancer_mock_nm> m_balancer_mock;
    gempba::node_manager* m_node_manager = nullptr;

    void SetUp() override { m_node_manager = new gempba::node_manager(&m_balancer_mock, nullptr); }

    void TearDown() override { delete m_node_manager; }
};


TEST_F(node_manager_test, try_update_result_maximise_accepts_better_score) {
    int v_result = 42;
    const gempba::score v_better = gempba::score::make(10);
    EXPECT_TRUE(m_node_manager->try_update_result(v_result, v_better));
}

TEST_F(node_manager_test, try_update_result_maximise_rejects_worse_score) {
    int v_result = 42;
    const gempba::score v_better = gempba::score::make(10);
    m_node_manager->try_update_result(v_result, v_better);

    int v_result2 = 99;
    const gempba::score v_worse = gempba::score::make(5);
    EXPECT_FALSE(m_node_manager->try_update_result(v_result2, v_worse));
}

TEST_F(node_manager_test, try_update_result_maximise_accepts_equal_score_only_first_time) {
    int v_result = 1;
    const gempba::score v_score = gempba::score::make(10);
    EXPECT_TRUE(m_node_manager->try_update_result(v_result, v_score));

    int v_result2 = 2;
    EXPECT_FALSE(m_node_manager->try_update_result(v_result2, v_score));
}

TEST_F(node_manager_test, try_update_result_minimise_accepts_smaller_score) {
    m_node_manager->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int v_result = 42;
    const gempba::score v_better = gempba::score::make(100);
    EXPECT_TRUE(m_node_manager->try_update_result(v_result, v_better));
}

TEST_F(node_manager_test, try_update_result_minimise_rejects_larger_score) {
    m_node_manager->set_goal(gempba::MINIMISE, gempba::score_type::I32);

    int v_result = 42;
    const gempba::score v_first = gempba::score::make(50);
    EXPECT_TRUE(m_node_manager->try_update_result(v_result, v_first));

    int v_result2 = 99;
    const gempba::score v_worse = gempba::score::make(100);
    EXPECT_FALSE(m_node_manager->try_update_result(v_result2, v_worse));
}

TEST_F(node_manager_test, try_update_result_with_serializer_accepts_better_score) {
    std::string v_result = "hello";
    const gempba::score v_better = gempba::score::make(10);
    std::function<gempba::task_packet(std::string&)> v_serializer = [](std::string& p_s) { return gempba::task_packet(p_s); };

    EXPECT_TRUE(m_node_manager->try_update_result(v_result, v_better, v_serializer));
}

TEST_F(node_manager_test, try_update_result_with_serializer_rejects_worse_score) {
    std::string v_first = "first";
    const gempba::score v_better = gempba::score::make(10);
    std::function<gempba::task_packet(std::string&)> v_serializer = [](std::string& p_s) { return gempba::task_packet(p_s); };
    m_node_manager->try_update_result(v_first, v_better, v_serializer);

    std::string v_second = "second";
    const gempba::score v_worse = gempba::score::make(5);
    EXPECT_FALSE(m_node_manager->try_update_result(v_second, v_worse, v_serializer));
}

TEST_F(node_manager_test, get_result_returns_nullopt_when_not_updated) {
    const auto v_result = m_node_manager->get_result<int>();
    EXPECT_FALSE(v_result.has_value());
}

TEST_F(node_manager_test, get_result_returns_value_after_update) {
    int v_val = 777;
    m_node_manager->try_update_result(v_val, gempba::score::make(5));

    const auto v_result = m_node_manager->get_result<int>();
    ASSERT_TRUE(v_result.has_value());
    EXPECT_EQ(777, v_result.value());
}

TEST_F(node_manager_test, get_result_returns_nullopt_when_stored_as_bytes) {
    std::string v_val = "data";
    const gempba::score v_score = gempba::score::make(5);
    std::function<gempba::task_packet(std::string&)> v_serializer = [](std::string& p_s) { return gempba::task_packet(p_s); };
    m_node_manager->try_update_result(v_val, v_score, v_serializer);

    const auto v_result = m_node_manager->get_result<std::string>();
    EXPECT_FALSE(v_result.has_value());
}

TEST_F(node_manager_test, get_result_bytes_returns_nullopt_when_stored_as_any) {
    int v_val = 42;
    m_node_manager->try_update_result(v_val, gempba::score::make(5));

    const auto v_bytes = m_node_manager->get_result_bytes();
    EXPECT_FALSE(v_bytes.has_value());
}

TEST_F(node_manager_test, get_result_bytes_returns_value_after_serializer_update) {
    std::string v_val = "test_data";
    const gempba::score v_score = gempba::score::make(10);
    std::function<gempba::task_packet(std::string&)> v_serializer = [](std::string& p_s) { return gempba::task_packet(p_s); };
    m_node_manager->try_update_result(v_val, v_score, v_serializer);

    const auto v_bytes = m_node_manager->get_result_bytes();
    ASSERT_TRUE(v_bytes.has_value());
    EXPECT_EQ(v_score, v_bytes->get_score());
}

TEST_F(node_manager_test, try_update_score_and_invalidate_result_updates_score) {
    int v_val = 42;
    m_node_manager->try_update_result(v_val, gempba::score::make(5));

    const gempba::score v_new_score = gempba::score::make(20);
    EXPECT_TRUE(m_node_manager->try_update_score_and_invalidate_result(v_new_score));
    EXPECT_EQ(v_new_score, m_node_manager->get_score());
}

TEST_F(node_manager_test, try_update_score_and_invalidate_result_rejects_worse) {
    int v_val = 42;
    const gempba::score v_first = gempba::score::make(20);
    m_node_manager->try_update_result(v_val, v_first);

    const gempba::score v_worse = gempba::score::make(5);
    EXPECT_FALSE(m_node_manager->try_update_score_and_invalidate_result(v_worse));
    EXPECT_EQ(v_first, m_node_manager->get_score());
}

TEST_F(node_manager_test, try_update_score_invalidates_any_result) {
    int v_val = 42;
    m_node_manager->try_update_result(v_val, gempba::score::make(5));

    ASSERT_TRUE(m_node_manager->get_result<int>().has_value());

    m_node_manager->try_update_score_and_invalidate_result(gempba::score::make(20));

    // result is now held as task_packet::EMPTY, so get_result<T>() returns nullopt
    EXPECT_FALSE(m_node_manager->get_result<int>().has_value());
    // but get_result_bytes() sees the empty packet as a valid (empty) result
    const auto v_bytes = m_node_manager->get_result_bytes();
    ASSERT_TRUE(v_bytes.has_value());
    EXPECT_EQ(gempba::task_packet::EMPTY, v_bytes->get_task_packet());
}

TEST_F(node_manager_test, set_score_and_get_score) {
    const gempba::score v_score = gempba::score::make(99);
    m_node_manager->set_score(v_score);
    EXPECT_EQ(v_score, m_node_manager->get_score());
}

TEST_F(node_manager_test, initial_score_is_int_min_for_maximise) {
    const gempba::score v_expected = gempba::score::make(INT_MIN);
    EXPECT_EQ(v_expected, m_node_manager->get_score());
}

TEST_F(node_manager_test, set_goal_minimise_resets_score_to_int_max) {
    m_node_manager->set_goal(gempba::MINIMISE, gempba::score_type::I32);
    const gempba::score v_expected = gempba::score::make(INT_MAX);
    EXPECT_EQ(v_expected, m_node_manager->get_score());
}

TEST_F(node_manager_test, rank_me_returns_minus_one_without_scheduler) { EXPECT_EQ(-1, m_node_manager->rank_me()); }

TEST_F(node_manager_test, get_wall_time_returns_positive_value) {
    const double v_time = gempba::node_manager::get_wall_time();
    EXPECT_GT(v_time, 0.0);
}

TEST_F(node_manager_test, set_thread_pool_size_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, set_thread_pool_size(4)).Times(1);
    m_node_manager->set_thread_pool_size(4);
}

TEST_F(node_manager_test, get_balancing_policy_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, get_balancing_policy()).WillOnce(testing::Return(gempba::WORK_STEALING));
    EXPECT_EQ(gempba::WORK_STEALING, m_node_manager->get_balancing_policy());
}

TEST_F(node_manager_test, generate_unique_id_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, generate_unique_id()).WillOnce(testing::Return(7u));
    EXPECT_EQ(7u, m_node_manager->generate_unique_id());
}

TEST_F(node_manager_test, get_thread_request_count_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, get_thread_request_count()).WillOnce(testing::Return(std::size_t{3}));
    EXPECT_EQ(3u, m_node_manager->get_thread_request_count());
}

TEST_F(node_manager_test, get_idle_time_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, get_idle_time()).WillOnce(testing::Return(1.5));
    EXPECT_DOUBLE_EQ(1.5, m_node_manager->get_idle_time());
}

TEST_F(node_manager_test, is_done_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, is_done()).WillOnce(testing::Return(true));
    EXPECT_TRUE(m_node_manager->is_done());
}

TEST(node_manager_with_real_balancer_test, forward_runs_unused_node_and_marks_it_forwarded) {
    gempba::work_stealing_load_balancer v_balancer;
    gempba::node_manager v_node_manager(&v_balancer, nullptr);

    bool v_was_invoked = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&v_was_invoked](std::thread::id, int, const gempba::node&) { v_was_invoked = true; };
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));
    gempba::node v_node(v_child);

    v_node_manager.forward(v_node);

    EXPECT_TRUE(v_was_invoked);
    EXPECT_EQ(gempba::FORWARDED, v_child->get_state());
}

TEST(node_manager_with_real_balancer_test, forward_skips_already_consumed_node) {
    gempba::work_stealing_load_balancer v_balancer;
    gempba::node_manager v_node_manager(&v_balancer, nullptr);

    bool v_was_invoked = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&v_was_invoked](std::thread::id, int, const gempba::node&) { v_was_invoked = true; };
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->set_state(gempba::FORWARDED); // already consumed
    gempba::node v_node(v_child);

    v_node_manager.forward(v_node);

    EXPECT_FALSE(v_was_invoked);
}

TEST_F(node_manager_test, wait_delegates_to_balancer) {
    EXPECT_CALL(m_balancer_mock, wait()).Times(1);
    EXPECT_NO_THROW(m_node_manager->wait());
}

TEST(node_manager_with_scheduler_test, constructor_queries_scheduler_for_rank_and_world_size) {
    testing::NiceMock<load_balancer_mock_nm> v_balancer;
    scheduler_worker_mock_nm v_scheduler;

    EXPECT_CALL(v_scheduler, rank_me()).Times(testing::AtLeast(1)).WillRepeatedly(testing::Return(3));
    EXPECT_CALL(v_scheduler, world_size()).Times(testing::AtLeast(1)).WillRepeatedly(testing::Return(8));

    const gempba::node_manager v_node_manager(&v_balancer, &v_scheduler);

    EXPECT_EQ(3, v_node_manager.rank_me());
}
