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
#include <map>
#include <memory>
#include <optional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <branch_handling/branch_handler.hpp>
#include <load_balancing/impl/work_stealing_load_balancer.hpp>
#include <runnables/api/serial_runnable.hpp>
#include <schedulers/api/scheduler.hpp>
#include <schedulers/api/stats.hpp>
#include <utils/transmission_guard.hpp>

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
    MOCK_METHOD(void, push, (gempba::task_packet &&p_task), (override));
    MOCK_METHOD(unsigned int, force_push, (gempba::task_packet &&p_task, int p_function_id), (override));
    MOCK_METHOD(std::optional<gempba::transmission_guard>, try_open_transmission_channel, (), (override));
    MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), ( const, override));
    MOCK_METHOD(void, run, (gempba::branch_handler &p_branch_handler, (std::function<std::shared_ptr<gempba::result_holder_parent>(gempba::task_packet)> &p_buffer_decoder)), ( override));
    MOCK_METHOD(unsigned int, next_process, (), (const, override));
};


class work_stealing_load_balancer_test : public ::testing::Test {
protected:
    void SetUp() override {
        m_scheduler_worker_mock = new scheduler_worker_mock();
    }

    void TearDown() override {
        delete m_scheduler_worker_mock;
    }

    scheduler_worker_mock *m_scheduler_worker_mock = nullptr;
};


TEST_F(work_stealing_load_balancer_test, get_strategy) {
    gempba::work_stealing_load_balancer v_load_balancer(m_scheduler_worker_mock);
    EXPECT_EQ(v_load_balancer.get_balancing_policy(), gempba::WORK_STEALING);
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
