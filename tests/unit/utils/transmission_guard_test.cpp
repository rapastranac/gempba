#include <atomic>
#include <chrono>
#include <gempba/utils/transmission_guard.hpp>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

class transmission_guard_test : public ::testing::Test {
protected:
    std::mutex m_test_mutex;
};

TEST_F(transmission_guard_test, constructs_with_locked_unique_lock) {
    std::unique_lock<std::mutex> v_lock(m_test_mutex);
    EXPECT_TRUE(v_lock.owns_lock());

    gempba::transmission_guard v_guard(std::move(v_lock));

    // After move, original lock should not own the lock
    EXPECT_FALSE(v_lock.owns_lock()); // NOLINT(bugprone-use-after-move)
}

TEST_F(transmission_guard_test, unlocks_mutex_on_destruction) {
    {
        std::unique_lock<std::mutex> v_lock(m_test_mutex);
        gempba::transmission_guard v_guard(std::move(v_lock));
        // Guard is holding the lock
        EXPECT_FALSE(m_test_mutex.try_lock());
    }
    // Guard destroyed, mutex should be unlocked
    EXPECT_TRUE(m_test_mutex.try_lock());
    m_test_mutex.unlock();
}

TEST_F(transmission_guard_test, supports_move_construction) {
    std::unique_lock<std::mutex> v_lock(m_test_mutex);
    gempba::transmission_guard v_guard1(std::move(v_lock));

    // Move construct
    gempba::transmission_guard v_guard2(std::move(v_guard1));

    // Original guard should no longer hold the lock
    EXPECT_FALSE(m_test_mutex.try_lock());
}

TEST_F(transmission_guard_test, supports_move_assignment) {
    std::mutex v_second_mutex;
    std::unique_lock<std::mutex> v_lock1(m_test_mutex);
    std::unique_lock<std::mutex> v_lock2(v_second_mutex);

    gempba::transmission_guard v_guard1(std::move(v_lock1));
    gempba::transmission_guard v_guard2(std::move(v_lock2));

    // Move assign
    v_guard1 = std::move(v_guard2);

    // guard1 should now hold second_mutex, test_mutex should be free
    EXPECT_TRUE(m_test_mutex.try_lock());
    m_test_mutex.unlock();
    EXPECT_FALSE(v_second_mutex.try_lock());
}

TEST_F(transmission_guard_test, prevents_concurrent_access) {
    std::atomic<bool> v_worker_has_lock{false};
    std::atomic<bool> v_thread_executed{false};
    bool v_main_thread_finished = false;

    std::thread v_worker([&]() {
        std::unique_lock<std::mutex> v_lock(m_test_mutex);
        gempba::transmission_guard v_guard(std::move(v_lock));
        v_worker_has_lock.store(true, std::memory_order_release);

        std::this_thread::sleep_for(50ms);
        v_thread_executed.store(true, std::memory_order_release);
    });

    while (!v_worker_has_lock.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // This should block until worker releases the lock
    const auto v_start = std::chrono::steady_clock::now();
    {
        std::unique_lock v_lock(m_test_mutex);
        gempba::transmission_guard v_guard(std::move(v_lock));
        v_main_thread_finished = true;
    }
    const auto v_end = std::chrono::steady_clock::now();

    v_worker.join();

    EXPECT_TRUE(v_thread_executed);
    EXPECT_TRUE(v_main_thread_finished);
    // Should have waited at least some time
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(v_end - v_start).count(), 30);
}

TEST_F(transmission_guard_test, works_with_try_lock) {
    std::unique_lock v_lock(m_test_mutex, std::try_to_lock);

    if (v_lock.owns_lock()) {
        gempba::transmission_guard v_guard(std::move(v_lock));
        EXPECT_FALSE(m_test_mutex.try_lock());
    } else {
        FAIL() << "Failed to acquire lock with try_to_lock";
    }
}
