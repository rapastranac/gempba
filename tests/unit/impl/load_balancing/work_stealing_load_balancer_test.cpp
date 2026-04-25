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
#include <impl/load_balancing/work_stealing_load_balancer.hpp>

using namespace std::chrono_literals;

/**
 * @author Andrés Pastrana
 * @date 2025-08-30
 */

class scheduler_worker_mock final : public gempba::scheduler::worker {
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


class work_stealing_load_balancer_test : public ::testing::Test {
protected:
    void SetUp() override { m_scheduler_worker_mock = new scheduler_worker_mock(); }

    void TearDown() override { delete m_scheduler_worker_mock; }

    scheduler_worker_mock *m_scheduler_worker_mock = nullptr;
};


TEST_F(work_stealing_load_balancer_test, get_strategy) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_EQ(gempba::WORK_STEALING, v_load_balancer.get_balancing_policy());
};

TEST_F(work_stealing_load_balancer_test, generate_unique_id) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    const unsigned int v_id1 = v_load_balancer.generate_unique_id();
    const unsigned int v_id2 = v_load_balancer.generate_unique_id();
    EXPECT_NE(v_id1, v_id2);
}

TEST_F(work_stealing_load_balancer_test, set_thread_pool_size) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    v_load_balancer.set_thread_pool_size(4);
    EXPECT_NO_THROW(v_load_balancer.set_thread_pool_size(2));
}

TEST_F(work_stealing_load_balancer_test, get_idle_time) {
    const gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_EQ(v_load_balancer.get_idle_time(), 0);
}

TEST_F(work_stealing_load_balancer_test, wait_and_is_done) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_NO_THROW(v_load_balancer.wait());
    EXPECT_TRUE(v_load_balancer.is_done());
}

TEST_F(work_stealing_load_balancer_test, force_local_submit_executes_function) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    bool v_executed = false;
    const auto v_future = v_load_balancer.force_local_submit([&v_executed]() -> std::any {
        v_executed = true;
        return std::any{};
    });
    v_future.wait();

    EXPECT_TRUE(v_executed);
}

TEST_F(work_stealing_load_balancer_test, force_local_submit_increments_thread_request_count) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    EXPECT_EQ(0u, v_load_balancer.get_thread_request_count());

    auto v_f1 = v_load_balancer.force_local_submit([]() -> std::any { return 1; });
    EXPECT_EQ(1u, v_load_balancer.get_thread_request_count());

    auto v_f2 = v_load_balancer.force_local_submit([]() -> std::any { return 2; });
    EXPECT_EQ(2u, v_load_balancer.get_thread_request_count());

    v_load_balancer.wait();
}

TEST_F(work_stealing_load_balancer_test, get_root_returns_empty_for_work_stealing) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    const auto v_root = v_load_balancer.get_root(std::this_thread::get_id());
    EXPECT_EQ(nullptr, v_root);
}

TEST_F(work_stealing_load_balancer_test, set_root_is_noop_for_work_stealing) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    std::shared_ptr<gempba::node_core> v_dummy;
    EXPECT_NO_THROW(v_load_balancer.set_root(std::this_thread::get_id(), v_dummy));
    EXPECT_EQ(nullptr, v_load_balancer.get_root(std::this_thread::get_id()));
}

TEST_F(work_stealing_load_balancer_test, forward_unused_initialized_node_calls_run_then_prunes) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_function_called = true; };
    int v_arg = 0;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));

    ASSERT_EQ(gempba::UNUSED, v_child->get_state());
    ASSERT_NE(nullptr, v_child->get_parent());

    gempba::node v_node(v_child);
    v_load_balancer.forward(v_node);

    EXPECT_TRUE(v_function_called);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, forward_consumed_node_only_prunes_no_run) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_function_called = true; };
    int v_arg = 0;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));
    v_child->set_state(gempba::FORWARDED);

    gempba::node v_node(v_child);
    v_load_balancer.forward(v_node);

    EXPECT_FALSE(v_function_called);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, forward_lazy_node_with_nullopt_initializer_only_prunes) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    bool v_function_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_function_called = true; };

    std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet &) { return std::make_tuple(0); };

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_load_balancer, v_parent, v_fn, v_initializer, v_serializer, v_deserializer);

    ASSERT_EQ(gempba::UNUSED, v_child->get_state());

    gempba::node v_node(v_child);
    v_load_balancer.forward(v_node);

    EXPECT_FALSE(v_function_called);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, try_local_submit_with_available_pool_executes_node) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::atomic<bool> v_function_called{false};
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_function_called = true; };
    int v_arg = 0;

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_local_submit(v_node);

    v_load_balancer.wait();

    EXPECT_TRUE(v_result);
    EXPECT_TRUE(v_function_called);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, try_local_submit_when_pool_full_falls_back_to_forward) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    v_load_balancer.set_thread_pool_size(1);

    std::mutex v_blocker_mutex;
    std::condition_variable v_started_cv;
    bool v_started = false;
    std::promise<void> v_release;
    auto v_release_future = v_release.get_future().share();

    v_load_balancer.force_local_submit([&v_blocker_mutex, &v_started, &v_started_cv, v_release_future]() mutable -> std::any {
        {
            std::scoped_lock v_lk(v_blocker_mutex);
            v_started = true;
        }
        v_started_cv.notify_one();
        v_release_future.wait();
        return {};
    });

    {
        std::unique_lock v_lk(v_blocker_mutex);
        v_started_cv.wait(v_lk, [&] { return v_started; });
    }

    // Single thread is now occupied — pool is full
    bool v_forward_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_forward_fn_called = true; };
    int v_arg = 0;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_local_submit(v_node);

    v_release.set_value();
    v_load_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_forward_fn_called); // forward() ran the node synchronously
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, try_remote_submit_throws_without_scheduler) {
    gempba::work_stealing_load_balancer v_load_balancer(nullptr);

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node &) {};
    int v_arg = 0;
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));

    gempba::node v_node(v_child);
    EXPECT_THROW(v_load_balancer.try_remote_submit(v_node, 0), std::runtime_error);
}

TEST_F(work_stealing_load_balancer_test, try_remote_submit_falls_back_to_local_when_no_channel) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    v_load_balancer.set_thread_pool_size(2);

    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce(testing::Return(std::nullopt));

    std::atomic<bool> v_fn_called{false};
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_fn_called = true; };
    int v_arg = 0;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg));

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_remote_submit(v_node, 0);

    v_load_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_called);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, try_remote_submit_delegates_remotely_when_channel_open) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });

    EXPECT_CALL(*m_scheduler_worker_mock, force_push(testing::_, testing::_)).WillOnce([](gempba::task_packet &&, int) { return 1u; });

    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet &) { return std::make_tuple(0); };

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node &) { FAIL(); };
    int v_arg = 0;
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(v_arg), v_serializer, v_deserializer);

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_remote_submit(v_node, 0);

    v_load_balancer.wait();

    EXPECT_TRUE(v_result);
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, send_local_throws_when_node_is_already_consumed) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node &) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->set_state(gempba::FORWARDED); // mark as already consumed

    gempba::node v_node(v_child);
    EXPECT_THROW(v_load_balancer.try_local_submit(v_node), std::runtime_error);
}

TEST_F(work_stealing_load_balancer_test, send_local_discards_lazy_node_when_not_worthy) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    bool v_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_fn_called = true; };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet &) { return std::make_tuple(0); };

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_load_balancer, v_parent, v_fn, v_initializer, v_serializer, v_deserializer);

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_local_submit(v_node);

    EXPECT_TRUE(v_result); // send() consumed the node even though it will not run
    EXPECT_FALSE(v_fn_called);
    EXPECT_EQ(gempba::DISCARDED, v_child->get_state());
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, send_local_returns_false_on_mutex_contention) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::mutex v_started_mutex;
    std::condition_variable v_started_cv;
    bool v_started = false;
    std::promise<void> v_release;
    const auto v_release_future = v_release.get_future().share();

    // Thread A calls try_remote_submit; the mock blocks while holding m_recursive_mutex
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        {
            std::scoped_lock v_lk(v_started_mutex);
            v_started = true;
        }
        v_started_cv.notify_one();
        v_release_future.wait();
        return std::nullopt;
    });

    std::function<void(std::thread::id, int, gempba::node)> v_fn_a = [](std::thread::id, int, const gempba::node &) {};
    auto v_parent_a = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child_a = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent_a, v_fn_a, std::make_tuple(0));
    gempba::node v_node_a(v_child_a);

    auto v_future_a = std::async(std::launch::async, [&] { v_load_balancer.try_remote_submit(v_node_a, 0); });

    {
        std::unique_lock v_lk(v_started_mutex);
        v_started_cv.wait(v_lk, [&] { return v_started; });
    }

    // Thread A holds m_recursive_mutex — send(node) must fail try_to_lock and fall back to forward()
    bool v_fn_b_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn_b = [&](std::thread::id, int, const gempba::node &) { v_fn_b_called = true; };
    auto v_parent_b = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child_b = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent_b, v_fn_b, std::make_tuple(0));
    gempba::node v_node_b(v_child_b);

    const bool v_result = v_load_balancer.try_local_submit(v_node_b);

    v_release.set_value();
    v_future_a.wait();
    v_load_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_b_called);
    EXPECT_EQ(nullptr, v_child_b->get_parent());
    EXPECT_EQ(nullptr, v_child_b->get_root());
}

TEST_F(work_stealing_load_balancer_test, send_remote_throws_when_node_is_already_consumed) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });

    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node &) {};
    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent, v_fn, std::make_tuple(0));
    v_child->set_state(gempba::FORWARDED); // mark as already consumed

    gempba::node v_node(v_child);
    EXPECT_THROW(v_load_balancer.try_remote_submit(v_node, 0), std::runtime_error);
}

TEST_F(work_stealing_load_balancer_test, send_remote_discards_lazy_node_when_not_worthy) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::mutex v_channel_mutex;
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        std::unique_lock v_lock(v_channel_mutex);
        return gempba::transmission_guard(std::move(v_lock));
    });

    bool v_fn_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [&](std::thread::id, int, const gempba::node &) { v_fn_called = true; };
    const std::function<std::optional<std::tuple<int>>()> v_initializer = []() -> std::optional<std::tuple<int>> { return std::nullopt; };
    const std::function<gempba::task_packet(int)> v_serializer = [](int) { return gempba::task_packet::EMPTY; };
    const std::function<std::tuple<int>(gempba::task_packet)> v_deserializer = [](const gempba::task_packet &) { return std::make_tuple(0); };

    auto v_parent = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child = gempba::node_core_impl<void(int)>::create_serializable_lazy(v_load_balancer, v_parent, v_fn, v_initializer, v_serializer, v_deserializer);

    gempba::node v_node(v_child);
    const bool v_result = v_load_balancer.try_remote_submit(v_node, 0);

    EXPECT_TRUE(v_result); // send(node, id) consumed the node even though it will not run
    EXPECT_FALSE(v_fn_called);
    EXPECT_EQ(gempba::DISCARDED, v_child->get_state());
    EXPECT_EQ(nullptr, v_child->get_parent());
    EXPECT_EQ(nullptr, v_child->get_root());
}

TEST_F(work_stealing_load_balancer_test, send_remote_returns_false_on_mutex_contention) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);

    std::mutex v_started_mutex;
    std::condition_variable v_started_cv;
    bool v_started = false;
    std::promise<void> v_release;
    const auto v_release_future = v_release.get_future().share();

    // Thread A holds m_recursive_mutex inside try_open_transmission_channel()
    EXPECT_CALL(*m_scheduler_worker_mock, try_open_transmission_channel()).WillOnce([&]() -> std::optional<gempba::transmission_guard> {
        {
            std::scoped_lock v_lk(v_started_mutex);
            v_started = true;
        }
        v_started_cv.notify_one();
        v_release_future.wait();
        return std::nullopt;
    });

    std::function<void(std::thread::id, int, gempba::node)> v_fn_a = [](std::thread::id, int, const gempba::node &) {};
    auto v_parent_a = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child_a = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent_a, v_fn_a, std::make_tuple(0));
    gempba::node v_node_a(v_child_a);

    auto v_future_a = std::async(std::launch::async, [&] { v_load_balancer.try_remote_submit(v_node_a, 0); });

    {
        std::unique_lock v_lk(v_started_mutex);
        v_started_cv.wait(v_lk, [&] { return v_started; });
    }

    // Thread A holds the lock — both send(node_b, id) and its fallback send(node_b) fail try_to_lock
    bool v_fn_b_called = false;
    std::function<void(std::thread::id, int, gempba::node)> v_fn_b = [&](std::thread::id, int, const gempba::node &) { v_fn_b_called = true; };
    auto v_parent_b = gempba::node_core_impl<void()>::create_dummy(v_load_balancer);
    auto v_child_b = gempba::node_core_impl<void(int)>::create_explicit(v_load_balancer, v_parent_b, v_fn_b, std::make_tuple(0));
    gempba::node v_node_b(v_child_b);

    const bool v_result = v_load_balancer.try_remote_submit(v_node_b, 0);

    v_release.set_value();
    v_future_a.wait();
    v_load_balancer.wait();

    EXPECT_FALSE(v_result);
    EXPECT_TRUE(v_fn_b_called);
    EXPECT_EQ(nullptr, v_child_b->get_parent());
    EXPECT_EQ(nullptr, v_child_b->get_root());
}
