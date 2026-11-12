#include <atomic>
#include <csignal>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#include <gempba/gempba.hpp>
#include <gempba/node_manager.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>
#include <impl/stop_state.hpp>

namespace {

    class stop_test : public ::testing::Test {
    protected:
        void SetUp() override { gempba::detail::clear_stop(); }

        void TearDown() override { gempba::detail::clear_stop(); }
    };

    std::unique_ptr<gempba::load_balancer> make_load_balancer(const gempba::balancing_policy p_policy) {
        if (p_policy == gempba::WORK_STEALING) {
            return std::make_unique<gempba::work_stealing_load_balancer>();
        }
        return std::make_unique<gempba::quasi_horizontal_load_balancer>();
    }

    class binary_tree_search {
    public:
        binary_tree_search(gempba::load_balancer& p_load_balancer, gempba::node_manager& p_node_manager, const int p_stop_at_visit) :
            m_load_balancer(p_load_balancer), m_node_manager(p_node_manager), m_stop_at_visit(p_stop_at_visit) {}

        void run(const int p_depth) {
            gempba::node v_seed = gempba::create_seed_node<void, int>(m_load_balancer, m_explore, std::make_tuple(p_depth));
            m_node_manager.try_local_submit(v_seed);
            m_node_manager.wait();
        }

        [[nodiscard]] int visits() const { return m_visits.load(); }

        /// @return the visits counted once the stop was requested, which nodes started before it may still add to
        [[nodiscard]] int visits_when_stopped() const { return m_visits_when_stopped.load(); }

    private:
        void explore(const int p_depth, const gempba::node& p_parent) {
            if (m_visits.fetch_add(1) + 1 == m_stop_at_visit) {
                gempba::request_stop();
                m_visits_when_stopped = m_visits.load();
            }
            if (p_depth == 0) {
                return;
            }
            gempba::node v_parent = p_parent == nullptr ? gempba::create_dummy_node(m_load_balancer) : p_parent;
            gempba::node v_left = gempba::create_explicit_node<void, int>(m_load_balancer, v_parent, m_explore, std::make_tuple(p_depth - 1));
            gempba::node v_right = gempba::create_explicit_node<void, int>(m_load_balancer, v_parent, m_explore, std::make_tuple(p_depth - 1));
            m_node_manager.try_local_submit(v_left);
            m_node_manager.forward(v_right);
        }

        gempba::load_balancer& m_load_balancer;
        gempba::node_manager& m_node_manager;
        const int m_stop_at_visit;
        std::atomic<int> m_visits{0};
        std::atomic<int> m_visits_when_stopped{0};
        std::function<void(std::thread::id, int, gempba::node)> m_explore = [this](std::thread::id, const int p_depth, const gempba::node& p_parent) { explore(p_depth, p_parent); };
    };

    class stop_pruning_test : public stop_test, public ::testing::WithParamInterface<gempba::balancing_policy> {};

} // namespace

TEST_F(stop_test, no_stop_is_requested_at_first) {
    EXPECT_FALSE(gempba::stop_requested());
    EXPECT_EQ(gempba::run_outcome{}, gempba::detail::requested_stop());
}

TEST_F(stop_test, request_stop_interrupts_by_code) {
    gempba::request_stop();

    EXPECT_TRUE(gempba::stop_requested());
    EXPECT_EQ((gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::CODE, 0}), gempba::detail::requested_stop());
}

TEST_F(stop_test, the_first_stop_requested_wins) {
    gempba::detail::request_stop(gempba::stop_trigger::SIGNAL, SIGTERM);
    gempba::request_stop();
    gempba::detail::request_stop(gempba::stop_trigger::CONTROL);

    EXPECT_EQ((gempba::run_outcome{gempba::end_reason::INTERRUPTED, gempba::stop_trigger::SIGNAL, SIGTERM}), gempba::detail::requested_stop());
}

TEST_F(stop_test, one_of_many_concurrent_requests_wins_whole) {
    constexpr int THREADS = 16;
    for (int v_round = 0; v_round < 200; ++v_round) {
        gempba::detail::clear_stop();
        std::promise<void> v_go;
        const std::shared_future<void> v_started = v_go.get_future().share();
        std::vector<std::thread> v_threads;
        for (int v_signal = 1; v_signal <= THREADS; ++v_signal) {
            v_threads.emplace_back([v_started, v_signal] {
                v_started.wait();
                gempba::detail::request_stop(gempba::stop_trigger::SIGNAL, v_signal);
            });
        }
        v_go.set_value();
        for (auto& v_thread: v_threads) {
            v_thread.join();
        }
        const gempba::run_outcome v_outcome = gempba::detail::requested_stop();
        ASSERT_EQ(gempba::stop_trigger::SIGNAL, v_outcome.m_trigger) << "round " << v_round;
        ASSERT_GE(v_outcome.m_signal, 1) << "round " << v_round;
        ASSERT_LE(v_outcome.m_signal, THREADS) << "round " << v_round;
    }
}

TEST_F(stop_test, shutdown_forgets_the_stop) {
    gempba::request_stop();

    gempba::shutdown();

    EXPECT_FALSE(gempba::stop_requested());
    EXPECT_EQ(gempba::run_outcome{}, gempba::detail::requested_stop());
}

TEST_P(stop_pruning_test, a_search_with_no_stop_visits_every_node) {
    const auto v_load_balancer = make_load_balancer(GetParam());
    v_load_balancer->set_thread_pool_size(4);
    gempba::node_manager v_node_manager(v_load_balancer.get(), nullptr);
    binary_tree_search v_search(*v_load_balancer, v_node_manager, -1);

    v_search.run(12);

    EXPECT_EQ(static_cast<int>((1U << 13U) - 1U), v_search.visits());
}

TEST_P(stop_pruning_test, a_stop_before_the_seed_runs_prunes_the_whole_search) {
    const auto v_load_balancer = make_load_balancer(GetParam());
    v_load_balancer->set_thread_pool_size(4);
    gempba::node_manager v_node_manager(v_load_balancer.get(), nullptr);
    binary_tree_search v_search(*v_load_balancer, v_node_manager, -1);

    gempba::request_stop();
    v_search.run(12);

    EXPECT_EQ(0, v_search.visits());
}

TEST_P(stop_pruning_test, a_stop_in_a_one_thread_search_prunes_every_node_after_it) {
    constexpr int STOP_AT_VISIT = 100;
    const auto v_load_balancer = make_load_balancer(GetParam());
    v_load_balancer->set_thread_pool_size(1);
    gempba::node_manager v_node_manager(v_load_balancer.get(), nullptr);
    binary_tree_search v_search(*v_load_balancer, v_node_manager, STOP_AT_VISIT);

    v_search.run(20);

    EXPECT_EQ(STOP_AT_VISIT, v_search.visits());
}

TEST_P(stop_pruning_test, a_stop_in_a_parallel_search_lets_only_the_nodes_already_started_finish) {
    constexpr int THREADS = 4;
    constexpr int STOP_AT_VISIT = 100;
    const auto v_load_balancer = make_load_balancer(GetParam());
    v_load_balancer->set_thread_pool_size(THREADS);
    gempba::node_manager v_node_manager(v_load_balancer.get(), nullptr);
    binary_tree_search v_search(*v_load_balancer, v_node_manager, STOP_AT_VISIT);

    v_search.run(20);

    EXPECT_GE(v_search.visits_when_stopped(), STOP_AT_VISIT);
    EXPECT_LE(v_search.visits(), v_search.visits_when_stopped() + THREADS);
}

TEST_P(stop_pruning_test, a_node_is_not_worth_branching_after_a_stop) {
    const auto v_load_balancer = make_load_balancer(GetParam());
    gempba::node v_parent = gempba::create_dummy_node(*v_load_balancer);
    std::function<void(std::thread::id, int, gempba::node)> v_function = [](std::thread::id, int, const gempba::node&) {};
    gempba::node v_node = gempba::create_explicit_node<void, int>(*v_load_balancer, v_parent, v_function, std::make_tuple(1));
    ASSERT_TRUE(v_node.should_branch());

    gempba::request_stop();

    EXPECT_FALSE(v_node.should_branch());
}

TEST_P(stop_pruning_test, a_node_queued_in_the_pool_skips_its_body_after_a_stop) {
    const auto v_load_balancer = make_load_balancer(GetParam());
    v_load_balancer->set_thread_pool_size(1);
    std::promise<void> v_release;
    const std::shared_future<void> v_released = v_release.get_future().share();
    std::promise<void> v_blocking;
    auto v_blocker = v_load_balancer->force_local_submit([v_released, &v_blocking]() -> std::any {
        v_blocking.set_value();
        v_released.wait();
        return {};
    });
    v_blocking.get_future().wait();

    std::atomic<bool> v_ran{false};
    gempba::node v_parent = gempba::create_dummy_node(*v_load_balancer);
    std::function<void(std::thread::id, int, gempba::node)> v_function = [&v_ran](std::thread::id, int, const gempba::node&) { v_ran = true; };
    gempba::node v_node = gempba::create_explicit_node<void, int>(*v_load_balancer, v_parent, v_function, std::make_tuple(1));
    v_node.delegate_locally(v_load_balancer.get());

    gempba::request_stop();
    v_release.set_value();
    v_load_balancer->wait();

    EXPECT_FALSE(v_ran.load());
}

INSTANTIATE_TEST_SUITE_P(both_balancers, stop_pruning_test, ::testing::Values(gempba::QUASI_HORIZONTAL, gempba::WORK_STEALING),
                         [](const ::testing::TestParamInfo<gempba::balancing_policy>& p_info) { return p_info.param == gempba::WORK_STEALING ? "work_stealing" : "quasi_horizontal"; });
