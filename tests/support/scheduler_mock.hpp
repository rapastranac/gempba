#ifndef GEMPBA_TESTS_SUPPORT_SCHEDULER_MOCK_HPP
#define GEMPBA_TESTS_SUPPORT_SCHEDULER_MOCK_HPP

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include <gmock/gmock.h>

#include <gempba/core/scheduler.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/node_manager.hpp>
#include <gempba/stats/stats.hpp>
#include <gempba/utils/result.hpp>
#include <gempba/utils/task_bundle.hpp>
#include <gempba/utils/task_packet.hpp>
#include <gempba/utils/transmission_guard.hpp>
#include <gempba/utils/tree.hpp>

namespace gempba::testing_support {

    /** @brief A gmock double of the whole @c gempba::scheduler interface, including the protected hooks behind run and the root's results. */
    class scheduler_mock : public scheduler {
    public:
        MOCK_METHOD(void, barrier, (), (override));
        MOCK_METHOD(int, rank_me, (), (const, override));
        MOCK_METHOD(int, world_size, (), (const, override));
        MOCK_METHOD(std::unique_ptr<stats>, get_stats, (), (const, override));
        MOCK_METHOD(double, elapsed_time, (), (const, override));
        MOCK_METHOD(std::size_t, get_pending_request_count, (), (const, override));
        MOCK_METHOD(void, set_goal, (goal, score_type), (override));
        MOCK_METHOD(void, set_custom_initial_topology, (tree&&), (override));
        MOCK_METHOD(std::vector<std::unique_ptr<stats>>, get_stats_vector, (), (const, override));
        MOCK_METHOD(void, synchronize_stats, (), (override));
        MOCK_METHOD(unsigned int, force_push, (task_packet&&, int), (override));
        MOCK_METHOD(std::optional<transmission_guard>, try_open_transmission_channel, (), (override));
        MOCK_METHOD(run_outcome, do_run, (node_manager&, (std::map<int, std::shared_ptr<serial_runnable>>), std::optional<task_bundle>), (override));
        MOCK_METHOD(task_packet, do_get_result, (), (override));
        MOCK_METHOD(std::vector<result>, do_get_all_results, (), (override));
    };

} // namespace gempba::testing_support

#endif // GEMPBA_TESTS_SUPPORT_SCHEDULER_MOCK_HPP
