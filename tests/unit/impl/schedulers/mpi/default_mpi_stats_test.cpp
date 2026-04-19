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
#include <cstring>
#include <gempba/stats/stats.hpp>
#include <gtest/gtest.h>
#include <impl/schedulers/default_mpi_stats.hpp>
#include <set>
#include <unordered_map>

TEST(default_mpi_stats_test, construction_and_defaults) {
    const gempba::default_mpi_stats v_stats_impl(5);

    EXPECT_EQ(5, v_stats_impl.m_rank);
    EXPECT_EQ(0u, v_stats_impl.m_received_task_count);
    EXPECT_EQ(0u, v_stats_impl.m_sent_task_count);
    EXPECT_EQ(0u, v_stats_impl.m_total_requested_tasks);
    EXPECT_EQ(0u, v_stats_impl.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(0.0, v_stats_impl.m_idle_time);
    EXPECT_DOUBLE_EQ(0.0, v_stats_impl.m_elapsed_time);
}

TEST(default_mpi_stats_test, visitor_returns_all_fields_correctly) {
    gempba::default_mpi_stats v_stats_impl(3);
    v_stats_impl.m_received_task_count = 10;
    v_stats_impl.m_sent_task_count = 20;
    v_stats_impl.m_total_requested_tasks = 30;
    v_stats_impl.m_total_thread_requests = 40;
    v_stats_impl.m_idle_time = 1.5;
    v_stats_impl.m_elapsed_time = 2.5;

    std::unordered_map<std::string, std::any> v_collected;

    v_stats_impl.visit([&](const std::string &p_key, std::any &&p_value) { v_collected[p_key] = std::move(p_value); });

    EXPECT_EQ(7, v_collected.size());
    EXPECT_EQ(3, std::any_cast<int>(v_collected["rank"]));
    EXPECT_EQ(10u, std::any_cast<std::size_t>(v_collected["received_task_count"]));
    EXPECT_EQ(20u, std::any_cast<std::size_t>(v_collected["sent_task_count"]));
    EXPECT_EQ(30u, std::any_cast<std::size_t>(v_collected["total_requested_tasks"]));
    EXPECT_EQ(40u, std::any_cast<std::size_t>(v_collected["total_thread_requests"]));
    EXPECT_DOUBLE_EQ(1.5, std::any_cast<double>(v_collected["idle_time"]));
    EXPECT_DOUBLE_EQ(2.5, std::any_cast<double>(v_collected["elapsed_time"]));
}

TEST(default_mpi_stats_test, serialize_and_from_packet_round_trip) {
    gempba::default_mpi_stats v_stats_impl(7);
    v_stats_impl.m_received_task_count = 11;
    v_stats_impl.m_sent_task_count = 22;
    v_stats_impl.m_total_requested_tasks = 33;
    v_stats_impl.m_total_thread_requests = 44;
    v_stats_impl.m_idle_time = 3.3;
    v_stats_impl.m_elapsed_time = 4.4;

    const gempba::task_packet v_pkt = v_stats_impl.serialize();
    const gempba::default_mpi_stats v_deserialized = gempba::default_mpi_stats::from_packet(v_pkt);

    EXPECT_EQ(7, v_deserialized.m_rank);
    EXPECT_EQ(11u, v_deserialized.m_received_task_count);
    EXPECT_EQ(22u, v_deserialized.m_sent_task_count);
    EXPECT_EQ(33u, v_deserialized.m_total_requested_tasks);
    EXPECT_EQ(44u, v_deserialized.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(3.3, v_deserialized.m_idle_time);
    EXPECT_DOUBLE_EQ(4.4, v_deserialized.m_elapsed_time);
}

TEST(default_mpi_stats_test, from_packet_invalid_size_throws) {
    const std::vector<std::byte> v_wrong_data(1); // too small
    const gempba::task_packet v_pkt(v_wrong_data);

    EXPECT_THROW(gempba::default_mpi_stats::from_packet(v_pkt), std::invalid_argument);
}

TEST(default_mpi_stats_test, copy_assignment_operator_same_rank) {
    gempba::default_mpi_stats v_stats_impl1(5);
    v_stats_impl1.m_received_task_count = 10;
    v_stats_impl1.m_sent_task_count = 20;
    v_stats_impl1.m_total_requested_tasks = 30;
    v_stats_impl1.m_total_thread_requests = 40;
    v_stats_impl1.m_idle_time = 1.5;
    v_stats_impl1.m_elapsed_time = 2.5;

    gempba::default_mpi_stats v_stats_impl2(5);
    v_stats_impl2.m_received_task_count = 1;
    v_stats_impl2.m_sent_task_count = 2;
    v_stats_impl2.m_total_requested_tasks = 3;
    v_stats_impl2.m_total_thread_requests = 4;
    v_stats_impl2.m_idle_time = 0.1;
    v_stats_impl2.m_elapsed_time = 0.2;

    v_stats_impl2 = v_stats_impl1;

    EXPECT_EQ(5, v_stats_impl2.m_rank);
    EXPECT_EQ(10u, v_stats_impl2.m_received_task_count);
    EXPECT_EQ(20u, v_stats_impl2.m_sent_task_count);
    EXPECT_EQ(30u, v_stats_impl2.m_total_requested_tasks);
    EXPECT_EQ(40u, v_stats_impl2.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(1.5, v_stats_impl2.m_idle_time);
    EXPECT_DOUBLE_EQ(2.5, v_stats_impl2.m_elapsed_time);
}

TEST(default_mpi_stats_test, copy_assignment_operator_self_assignment) {
    gempba::default_mpi_stats v_stats_impl(3);
    v_stats_impl.m_received_task_count = 15;
    v_stats_impl.m_sent_task_count = 25;
    v_stats_impl.m_total_requested_tasks = 35;
    v_stats_impl.m_total_thread_requests = 40;
    v_stats_impl.m_idle_time = 2.5;
    v_stats_impl.m_elapsed_time = 3.5;

    v_stats_impl = v_stats_impl;

    EXPECT_EQ(3, v_stats_impl.m_rank);
    EXPECT_EQ(15u, v_stats_impl.m_received_task_count);
    EXPECT_EQ(25u, v_stats_impl.m_sent_task_count);
    EXPECT_EQ(35u, v_stats_impl.m_total_requested_tasks);
    EXPECT_EQ(40u, v_stats_impl.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(2.5, v_stats_impl.m_idle_time);
    EXPECT_DOUBLE_EQ(3.5, v_stats_impl.m_elapsed_time);
}

TEST(default_mpi_stats_test, move_assignment_operator_same_rank) {
    gempba::default_mpi_stats v_stats_impl1(8);
    v_stats_impl1.m_received_task_count = 100;
    v_stats_impl1.m_sent_task_count = 200;
    v_stats_impl1.m_total_requested_tasks = 300;
    v_stats_impl1.m_total_thread_requests = 400;
    v_stats_impl1.m_idle_time = 10.5;
    v_stats_impl1.m_elapsed_time = 20.5;

    gempba::default_mpi_stats v_stats_impl2(8);

    v_stats_impl2 = std::move(v_stats_impl1);

    EXPECT_EQ(8, v_stats_impl2.m_rank);
    EXPECT_EQ(100u, v_stats_impl2.m_received_task_count);
    EXPECT_EQ(200u, v_stats_impl2.m_sent_task_count);
    EXPECT_EQ(300u, v_stats_impl2.m_total_requested_tasks);
    EXPECT_EQ(400u, v_stats_impl2.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(10.5, v_stats_impl2.m_idle_time);
    EXPECT_DOUBLE_EQ(20.5, v_stats_impl2.m_elapsed_time);
}

TEST(default_mpi_stats_test, move_assignment_operator_self_assignment) {
    gempba::default_mpi_stats v_stats_impl(9);
    v_stats_impl.m_received_task_count = 50;
    v_stats_impl.m_sent_task_count = 60;
    v_stats_impl.m_total_requested_tasks = 70;
    v_stats_impl.m_total_thread_requests = 80;
    v_stats_impl.m_idle_time = 5.5;
    v_stats_impl.m_elapsed_time = 6.5;

    v_stats_impl = std::move(v_stats_impl);

    EXPECT_EQ(9, v_stats_impl.m_rank);
    EXPECT_EQ(50u, v_stats_impl.m_received_task_count);
    EXPECT_EQ(60u, v_stats_impl.m_sent_task_count);
    EXPECT_EQ(70u, v_stats_impl.m_total_requested_tasks);
    EXPECT_EQ(80u, v_stats_impl.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(5.5, v_stats_impl.m_idle_time);
    EXPECT_DOUBLE_EQ(6.5, v_stats_impl.m_elapsed_time);
}

TEST(default_mpi_stats_test, copy_constructor) {
    gempba::default_mpi_stats v_stats_impl1(4);
    v_stats_impl1.m_received_task_count = 40;
    v_stats_impl1.m_sent_task_count = 50;
    v_stats_impl1.m_total_requested_tasks = 60;
    v_stats_impl1.m_total_thread_requests = 70;
    v_stats_impl1.m_idle_time = 4.5;
    v_stats_impl1.m_elapsed_time = 5.5;

    const gempba::default_mpi_stats v_stats_impl2(v_stats_impl1);

    EXPECT_EQ(4, v_stats_impl2.m_rank);
    EXPECT_EQ(40u, v_stats_impl2.m_received_task_count);
    EXPECT_EQ(50u, v_stats_impl2.m_sent_task_count);
    EXPECT_EQ(60u, v_stats_impl2.m_total_requested_tasks);
    EXPECT_EQ(70u, v_stats_impl2.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(4.5, v_stats_impl2.m_idle_time);
    EXPECT_DOUBLE_EQ(5.5, v_stats_impl2.m_elapsed_time);
}

TEST(default_mpi_stats_test, move_constructor) {
    gempba::default_mpi_stats v_stats_impl1(6);
    v_stats_impl1.m_received_task_count = 80;
    v_stats_impl1.m_sent_task_count = 90;
    v_stats_impl1.m_total_requested_tasks = 100;
    v_stats_impl1.m_total_thread_requests = 110;
    v_stats_impl1.m_idle_time = 8.5;
    v_stats_impl1.m_elapsed_time = 9.5;

    const gempba::default_mpi_stats v_stats_impl2(std::move(v_stats_impl1));

    EXPECT_EQ(6, v_stats_impl2.m_rank);
    EXPECT_EQ(80u, v_stats_impl2.m_received_task_count);
    EXPECT_EQ(90u, v_stats_impl2.m_sent_task_count);
    EXPECT_EQ(100u, v_stats_impl2.m_total_requested_tasks);
    EXPECT_EQ(110u, v_stats_impl2.m_total_thread_requests);
    EXPECT_DOUBLE_EQ(8.5, v_stats_impl2.m_idle_time);
    EXPECT_DOUBLE_EQ(9.5, v_stats_impl2.m_elapsed_time);
}

TEST(stats_interface_test, serialize_deserialize_comparison_identical_objects) {
    gempba::default_mpi_stats v_stats_impl1(3);
    v_stats_impl1.m_received_task_count = 15;
    v_stats_impl1.m_sent_task_count = 25;
    v_stats_impl1.m_total_requested_tasks = 35;
    v_stats_impl1.m_total_thread_requests = 40;
    v_stats_impl1.m_idle_time = 2.5;
    v_stats_impl1.m_elapsed_time = 3.5;

    gempba::default_mpi_stats v_stats_impl2(3);
    v_stats_impl2.m_received_task_count = 15;
    v_stats_impl2.m_sent_task_count = 25;
    v_stats_impl2.m_total_requested_tasks = 35;
    v_stats_impl2.m_total_thread_requests = 40;
    v_stats_impl2.m_idle_time = 2.5;
    v_stats_impl2.m_elapsed_time = 3.5;

    const gempba::stats &v_stats_ref1 = v_stats_impl1;
    const gempba::stats &v_stats_ref2 = v_stats_impl2;

    const gempba::task_packet v_packet1 = v_stats_ref1.serialize();
    const gempba::task_packet v_packet2 = v_stats_ref2.serialize();

    EXPECT_EQ(v_packet2.size(), v_packet1.size());
    EXPECT_TRUE(std::memcmp(v_packet1.data(), v_packet2.data(), v_packet1.size()) == 0);
}

TEST(stats_interface_test, visitor_comparison_identical_objects) {
    gempba::default_mpi_stats v_stats_impl1(7);
    v_stats_impl1.m_received_task_count = 100;
    v_stats_impl1.m_sent_task_count = 200;
    v_stats_impl1.m_total_requested_tasks = 300;
    v_stats_impl1.m_total_thread_requests = 400;
    v_stats_impl1.m_idle_time = 10.5;
    v_stats_impl1.m_elapsed_time = 20.5;

    gempba::default_mpi_stats v_stats_impl2(7);
    v_stats_impl2.m_received_task_count = 100;
    v_stats_impl2.m_sent_task_count = 200;
    v_stats_impl2.m_total_requested_tasks = 300;
    v_stats_impl2.m_total_thread_requests = 400;
    v_stats_impl2.m_idle_time = 10.5;
    v_stats_impl2.m_elapsed_time = 20.5;

    const gempba::stats &v_stats_ref1 = v_stats_impl1;
    const gempba::stats &v_stats_ref2 = v_stats_impl2;

    std::unordered_map<std::string, std::any> v_collected1;
    std::unordered_map<std::string, std::any> v_collected2;

    v_stats_ref1.visit([&](const std::string &p_key, std::any &&p_value) { v_collected1[p_key] = std::move(p_value); });

    v_stats_ref2.visit([&](const std::string &p_key, std::any &&p_value) { v_collected2[p_key] = std::move(p_value); });

    EXPECT_EQ(v_collected2.size(), v_collected1.size());
    EXPECT_EQ(7, v_collected1.size());

    for (const auto &[v_key, v_value]: v_collected1) {
        EXPECT_TRUE(v_collected2.contains(v_key));
        const auto &v_other_value = v_collected2.at(v_key);

        if (v_key == "rank") {
            EXPECT_EQ(std::any_cast<int>(v_other_value), std::any_cast<int>(v_value));
        } else if (v_key == "idle_time" || v_key == "elapsed_time") {
            EXPECT_DOUBLE_EQ(std::any_cast<double>(v_other_value), std::any_cast<double>(v_value));
        } else {
            EXPECT_EQ(std::any_cast<std::size_t>(v_other_value), std::any_cast<std::size_t>(v_value));
        }
    }
}

TEST(stats_interface_test, polymorphic_behavior_through_interface) {
    std::vector<std::unique_ptr<gempba::stats>> v_stats_objects;

    auto v_stats1 = std::make_unique<gempba::default_mpi_stats>(1);
    v_stats1->m_received_task_count = 10;
    v_stats1->m_idle_time = 1.0;

    auto v_stats2 = std::make_unique<gempba::default_mpi_stats>(2);
    v_stats2->m_received_task_count = 20;
    v_stats2->m_idle_time = 2.0;

    v_stats_objects.push_back(std::move(v_stats1));
    v_stats_objects.push_back(std::move(v_stats2));

    std::set<gempba::task_packet> v_packet_set;
    for (const auto &v_stats: v_stats_objects) {
        gempba::task_packet v_packet = v_stats->serialize();
        auto [v_iterator, v_inserted] = v_packet_set.insert(v_packet);
        EXPECT_TRUE(v_inserted) << "Packet collision detected - two different objects produced identical packets";
    }

    EXPECT_EQ(2u, v_packet_set.size());

    int v_rank_sum = 0;
    std::size_t v_task_count_sum = 0;

    for (const auto &v_stats: v_stats_objects) {
        v_stats->visit([&](const std::string &p_key, std::any &&p_value) {
            if (p_key == "rank") {
                v_rank_sum += std::any_cast<int>(p_value);
            } else if (p_key == "received_task_count") {
                v_task_count_sum += std::any_cast<std::size_t>(p_value);
            }
        });
    }

    EXPECT_EQ(3, v_rank_sum);
    EXPECT_EQ(30u, v_task_count_sum);
}
