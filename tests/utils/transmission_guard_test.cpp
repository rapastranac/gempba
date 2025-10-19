#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <chrono>
#include <gempba/utils/transmission_guard.hpp>

using namespace std::chrono_literals;

class transmission_guard_test : public ::testing::Test {
protected:
    std::mutex test_mutex;
};

TEST_F(transmission_guard_test, constructs_with_locked_unique_lock) {
    std::unique_lock<std::mutex> lock(test_mutex);
    EXPECT_TRUE(lock.owns_lock());

    gempba::transmission_guard guard(std::move(lock));

    // After move, original lock should not own the lock
    EXPECT_FALSE(lock.owns_lock());
}

TEST_F(transmission_guard_test, unlocks_mutex_on_destruction) {
    {
        std::unique_lock<std::mutex> lock(test_mutex);
        gempba::transmission_guard guard(std::move(lock));
        // Guard is holding the lock
        EXPECT_FALSE(test_mutex.try_lock());
    }
    // Guard destroyed, mutex should be unlocked
    EXPECT_TRUE(test_mutex.try_lock());
    test_mutex.unlock();
}

TEST_F(transmission_guard_test, supports_move_construction) {
    std::unique_lock<std::mutex> lock(test_mutex);
    gempba::transmission_guard guard1(std::move(lock));

    // Move construct
    gempba::transmission_guard guard2(std::move(guard1));

    // Original guard should no longer hold the lock
    EXPECT_FALSE(test_mutex.try_lock());
}

TEST_F(transmission_guard_test, supports_move_assignment) {
    std::mutex second_mutex;
    std::unique_lock<std::mutex> lock1(test_mutex);
    std::unique_lock<std::mutex> lock2(second_mutex);

    gempba::transmission_guard guard1(std::move(lock1));
    gempba::transmission_guard guard2(std::move(lock2));

    // Move assign
    guard1 = std::move(guard2);

    // guard1 should now hold second_mutex, test_mutex should be free
    EXPECT_TRUE(test_mutex.try_lock());
    test_mutex.unlock();
    EXPECT_FALSE(second_mutex.try_lock());
}

TEST_F(transmission_guard_test, prevents_concurrent_access) {
    bool thread_executed = false;
    bool main_thread_finished = false;

    std::thread worker([&]() {
        std::unique_lock<std::mutex> lock(test_mutex);
        gempba::transmission_guard guard(std::move(lock));

        // Wait for main thread to try accessing
        std::this_thread::sleep_for(50ms);
        thread_executed = true;
    });

    // Give worker thread time to acquire lock
    std::this_thread::sleep_for(10ms);

    // This should block until worker releases the lock
    const auto start = std::chrono::steady_clock::now();
    {
        std::unique_lock lock(test_mutex);
        gempba::transmission_guard guard(std::move(lock));
        main_thread_finished = true;
    }
    const auto end = std::chrono::steady_clock::now();

    worker.join();

    EXPECT_TRUE(thread_executed);
    EXPECT_TRUE(main_thread_finished);
    // Should have waited at least some time
    EXPECT_GT(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count(), 30);
}

TEST_F(transmission_guard_test, works_with_try_lock) {
    std::unique_lock lock(test_mutex, std::try_to_lock);

    if (lock.owns_lock()) {
        gempba::transmission_guard guard(std::move(lock));
        EXPECT_FALSE(test_mutex.try_lock());
    } else {
        FAIL() << "Failed to acquire lock with try_to_lock";
    }
}
