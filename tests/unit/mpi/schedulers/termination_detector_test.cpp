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

#include <gtest/gtest.h>
#include <impl/schedulers/termination_detector.hpp>

#include <algorithm>
#include <cstddef>
#include <random>
#include <vector>

namespace {
    using gempba::termination_detector;

    TEST(termination_detector_test, first_task_while_disengaged_defers_its_ack) {
        termination_detector v_detector(false);
        EXPECT_EQ(v_detector.on_task_received(3), std::nullopt);
        EXPECT_EQ(v_detector.parent(), 3);
    }

    TEST(termination_detector_test, task_while_engaged_is_acked_on_receipt) {
        termination_detector v_detector(false);
        ASSERT_EQ(v_detector.on_task_received(3), std::nullopt);
        EXPECT_EQ(v_detector.on_task_received(5), 5);
        EXPECT_EQ(v_detector.parent(), 3);
    }

    TEST(termination_detector_test, root_acks_every_task_on_receipt) {
        termination_detector v_root(true);
        EXPECT_EQ(v_root.on_task_received(2), 2);
        EXPECT_EQ(v_root.parent(), termination_detector::NO_PARENT);
        EXPECT_EQ(v_root.on_passive(), std::nullopt);
    }

    TEST(termination_detector_test, passive_rank_keeps_its_parent_while_sends_are_unacknowledged) {
        termination_detector v_detector(false);
        ASSERT_EQ(v_detector.on_task_received(1), std::nullopt);
        v_detector.on_task_sent();
        EXPECT_EQ(v_detector.on_passive(), std::nullopt);
        EXPECT_FALSE(v_detector.is_quiescent());

        EXPECT_TRUE(v_detector.on_ack_received());
        EXPECT_EQ(v_detector.on_passive(), 1);
        EXPECT_TRUE(v_detector.is_quiescent());
    }

    TEST(termination_detector_test, ack_without_an_outstanding_send_is_rejected) {
        termination_detector v_detector(false);
        EXPECT_FALSE(v_detector.on_ack_received());
        EXPECT_EQ(v_detector.deficit(), 0);
    }

    TEST(termination_detector_test, disengaged_rank_has_no_parent_to_release) {
        termination_detector v_detector(false);
        EXPECT_EQ(v_detector.on_passive(), std::nullopt);
        EXPECT_TRUE(v_detector.is_quiescent());
    }

    TEST(termination_detector_test, worker_reactivated_after_its_peers_went_idle_keeps_the_root_waiting) {
        constexpr int CENTER = 0;
        constexpr int P = 1;
        constexpr int W = 2;
        termination_detector v_center(true);
        termination_detector v_p(false);
        termination_detector v_w(false);
        termination_detector v_q(false);

        v_center.on_task_sent();
        ASSERT_EQ(v_p.on_task_received(CENTER), std::nullopt);
        v_p.on_task_sent();
        ASSERT_EQ(v_w.on_task_received(P), std::nullopt);
        ASSERT_EQ(v_p.on_passive(), std::nullopt);

        v_w.on_task_sent();
        ASSERT_EQ(v_q.on_task_received(W), std::nullopt);
        ASSERT_EQ(v_w.on_passive(), std::nullopt);

        EXPECT_EQ(v_center.deficit(), 1);

        ASSERT_EQ(v_q.on_passive(), W);
        ASSERT_TRUE(v_w.on_ack_received());
        ASSERT_EQ(v_w.on_passive(), P);
        ASSERT_TRUE(v_p.on_ack_received());
        ASSERT_EQ(v_p.on_passive(), CENTER);
        ASSERT_TRUE(v_center.on_ack_received());

        EXPECT_EQ(v_center.deficit(), 0);
        EXPECT_TRUE(v_p.is_quiescent());
        EXPECT_TRUE(v_w.is_quiescent());
        EXPECT_TRUE(v_q.is_quiescent());
    }

    struct sim_message {
        bool m_is_task;
        int m_from;
        int m_to;
        int m_work;
    };

    /**
     * Workers consume work units or split them off. With a seeding root (semi-centralized) rank 0 only sends the seed
     * and splits go to other workers; with a relaying root (centralized) splits go to rank 0, which queues them and
     * dispatches them to workers. In-flight messages are delivered in a random order, which is a stronger adversary than
     * MPI's per-pair FIFO.
     */
    void simulate(const unsigned p_seed, const int p_world_size, const int p_seed_work, const bool p_root_relays) {
        std::mt19937 v_rng(p_seed);
        std::vector<termination_detector> v_detectors;
        v_detectors.reserve(static_cast<std::size_t>(p_world_size));
        for (int v_rank = 0; v_rank < p_world_size; ++v_rank) {
            v_detectors.emplace_back(v_rank == 0);
        }
        std::vector<int> v_work(static_cast<std::size_t>(p_world_size), 0);
        std::vector<int> v_root_queue;
        std::vector<sim_message> v_in_flight;
        int v_consumed = 0;
        bool v_detected = false;

        const auto v_release_if_passive = [&](const int p_rank) {
            if (v_work[p_rank] != 0) {
                return;
            }
            if (const auto v_parent = v_detectors[p_rank].on_passive()) {
                v_in_flight.push_back({false, p_rank, *v_parent, 0});
            }
        };

        v_detectors[0].on_task_sent();
        v_in_flight.push_back({true, 0, 1, p_seed_work});

        while (true) {
            std::vector<int> v_active;
            for (int v_rank = 1; v_rank < p_world_size; ++v_rank) {
                if (v_work[v_rank] > 0) {
                    v_active.push_back(v_rank);
                }
            }
            const bool v_root_can_dispatch = !v_root_queue.empty();
            const std::size_t v_event_count = v_in_flight.size() + v_active.size() + (v_root_can_dispatch ? 1 : 0);
            if (v_event_count == 0) {
                break;
            }
            ASSERT_FALSE(v_detected);

            const std::size_t v_event = std::uniform_int_distribution<std::size_t>(0, v_event_count - 1)(v_rng);
            if (v_event < v_in_flight.size()) {
                const sim_message v_message = v_in_flight[v_event];
                v_in_flight.erase(v_in_flight.begin() + static_cast<std::ptrdiff_t>(v_event));
                if (v_message.m_is_task) {
                    if (v_message.m_to == 0) {
                        v_root_queue.push_back(v_message.m_work);
                    } else {
                        v_work[v_message.m_to] += v_message.m_work;
                    }
                    if (const auto v_ack = v_detectors[v_message.m_to].on_task_received(v_message.m_from)) {
                        v_in_flight.push_back({false, v_message.m_to, *v_ack, 0});
                    }
                    continue;
                }
                ASSERT_TRUE(v_detectors[v_message.m_to].on_ack_received());
                if (v_message.m_to != 0) {
                    v_release_if_passive(v_message.m_to);
                    continue;
                }
                if (v_detectors[0].deficit() == 0 && v_root_queue.empty()) {
                    v_detected = true;
                    ASSERT_TRUE(v_in_flight.empty());
                    ASSERT_TRUE(std::ranges::all_of(v_work, [](const int p_units) { return p_units == 0; }));
                    ASSERT_TRUE(std::ranges::all_of(v_detectors, [](const termination_detector& p_detector) { return p_detector.is_quiescent(); }));
                    ASSERT_EQ(v_consumed, p_seed_work);
                }
                continue;
            }

            if (v_event == v_in_flight.size() + v_active.size()) {
                const std::size_t v_index = std::uniform_int_distribution<std::size_t>(0, v_root_queue.size() - 1)(v_rng);
                const int v_task_work = v_root_queue[v_index];
                v_root_queue.erase(v_root_queue.begin() + static_cast<std::ptrdiff_t>(v_index));
                v_detectors[0].on_task_sent();
                v_in_flight.push_back({true, 0, std::uniform_int_distribution(1, p_world_size - 1)(v_rng), v_task_work});
                continue;
            }

            const int v_rank = v_active[v_event - v_in_flight.size()];
            const bool v_can_split = (p_root_relays || p_world_size > 2) && v_work[v_rank] >= 2;
            if (v_can_split && std::bernoulli_distribution(0.5)(v_rng)) {
                int v_destination = 0;
                if (!p_root_relays) {
                    v_destination = std::uniform_int_distribution(1, p_world_size - 2)(v_rng);
                    if (v_destination >= v_rank) {
                        ++v_destination;
                    }
                }
                const int v_split = std::uniform_int_distribution(1, v_work[v_rank] - 1)(v_rng);
                v_work[v_rank] -= v_split;
                v_detectors[v_rank].on_task_sent();
                v_in_flight.push_back({true, v_rank, v_destination, v_split});
                continue;
            }
            --v_work[v_rank];
            ++v_consumed;
            v_release_if_passive(v_rank);
        }

        ASSERT_TRUE(v_detected);
    }

    void simulate_many(const bool p_root_relays) {
        for (const int v_world_size: {2, 3, 4, 8}) {
            for (const int v_seed_work: {1, 5, 40}) {
                for (unsigned v_seed = 0; v_seed < 300; ++v_seed) {
                    SCOPED_TRACE(testing::Message() << "world_size=" << v_world_size << " seed_work=" << v_seed_work << " seed=" << v_seed);
                    simulate(v_seed, v_world_size, v_seed_work, p_root_relays);
                    if (testing::Test::HasFatalFailure()) {
                        return;
                    }
                }
            }
        }
    }

    TEST(termination_detector_test, root_detects_termination_exactly_when_all_work_is_done_under_any_delivery_order) { simulate_many(false); }

    TEST(termination_detector_test, relaying_root_detects_termination_exactly_when_all_work_is_done_under_any_delivery_order) { simulate_many(true); }

} // namespace
