#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gempba/core/scheduler.hpp>
#include <gempba/node_manager.hpp>
#include <gempba/utils/result.hpp>
#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>
#include <scheduler_mock.hpp>

namespace {
    using ::testing::_;
    using ::testing::NiceMock;
    using ::testing::Return;
    using runnable_map = std::map<int, std::shared_ptr<gempba::serial_runnable>>;

    class scheduler_test : public ::testing::Test {
    protected:
        NiceMock<gempba::testing_support::scheduler_mock> m_scheduler;
        gempba::node_manager m_node_manager{nullptr, nullptr};

        void on_rank(const int p_rank) { ON_CALL(m_scheduler, rank_me()).WillByDefault(Return(p_rank)); }

        static gempba::task_bundle seed() { return gempba::task_bundle(gempba::task_packet("seed"), 7); }
    };

    TEST_F(scheduler_test, rank_0_is_the_root) {
        on_rank(0);
        EXPECT_TRUE(m_scheduler.is_root());
        on_rank(3);
        EXPECT_FALSE(m_scheduler.is_root());
    }

    TEST_F(scheduler_test, run_hands_the_root_its_seed) {
        on_rank(0);
        EXPECT_CALL(m_scheduler, do_run(_, _, std::optional{seed()}));
        m_scheduler.run(m_node_manager, runnable_map{}, seed());
    }

    TEST_F(scheduler_test, run_on_another_rank_passes_no_seed) {
        on_rank(2);
        EXPECT_CALL(m_scheduler, do_run(_, _, std::optional<gempba::task_bundle>{}));
        m_scheduler.run(m_node_manager, runnable_map{});
    }

    TEST_F(scheduler_test, run_rejects_a_root_without_a_seed) {
        on_rank(0);
        EXPECT_CALL(m_scheduler, do_run(_, _, _)).Times(0);
        EXPECT_THROW(m_scheduler.run(m_node_manager, runnable_map{}), std::invalid_argument);
    }

    TEST_F(scheduler_test, run_rejects_a_seed_off_the_root) {
        on_rank(1);
        EXPECT_CALL(m_scheduler, do_run(_, _, _)).Times(0);
        EXPECT_THROW(m_scheduler.run(m_node_manager, runnable_map{}, seed()), std::invalid_argument);
    }

    TEST_F(scheduler_test, the_root_reads_the_results) {
        on_rank(0);
        const gempba::task_packet v_best("best");
        EXPECT_CALL(m_scheduler, do_get_result()).WillOnce(Return(v_best));
        EXPECT_CALL(m_scheduler, do_get_all_results()).WillOnce(Return(std::vector<gempba::result>{gempba::result::EMPTY, gempba::result::EMPTY}));

        EXPECT_EQ(v_best, m_scheduler.get_result());
        EXPECT_EQ(2u, m_scheduler.get_all_results().size());
    }

    TEST_F(scheduler_test, results_are_only_for_the_root) {
        on_rank(1);
        EXPECT_CALL(m_scheduler, do_get_result()).Times(0);
        EXPECT_CALL(m_scheduler, do_get_all_results()).Times(0);

        EXPECT_THROW([[maybe_unused]] auto v_result = m_scheduler.get_result(), std::logic_error);
        EXPECT_THROW([[maybe_unused]] auto v_results = m_scheduler.get_all_results(), std::logic_error);
    }
} // namespace
