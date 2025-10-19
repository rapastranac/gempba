#include <gtest/gtest.h>
#include <schedulers/defaults/default_mpi_stats_visitor.hpp>
#include <impl/schedulers/default_mpi_stats.hpp>

TEST(default_mpi_stats_visitor_test, collects_expected_fields) {
    gempba::default_mpi_stats v_stats(42);
    v_stats.m_received_task_count = 10;
    v_stats.m_sent_task_count = 20;
    v_stats.m_total_requested_tasks = 30;
    v_stats.m_total_thread_requests = 40;
    v_stats.m_idle_time = 1.5;
    v_stats.m_elapsed_time = 2.5;

    const auto v_visitor = std::make_unique<gempba::default_mpi_stats_visitor>();
    v_stats.visit(v_visitor.get());

    EXPECT_EQ(10u, v_visitor->m_received_task_count);
    EXPECT_EQ(20u, v_visitor->m_sent_task_count);
    EXPECT_EQ(30u, v_visitor->m_total_requested_tasks);
    EXPECT_EQ(40u, v_visitor->m_total_thread_requests);
    EXPECT_DOUBLE_EQ(1.5, v_visitor->m_idle_time);
    EXPECT_DOUBLE_EQ(2.5, v_visitor->m_elapsed_time);
}

TEST(default_mpi_stats_visitor_test, handles_default_constructed_stats) {
    const gempba::default_mpi_stats v_stats(7); // all other fields default

    const auto v_visitor = std::make_unique<gempba::default_mpi_stats_visitor>();
    v_stats.visit(v_visitor.get());

    EXPECT_EQ(0u, v_visitor->m_received_task_count);
    EXPECT_EQ(0u, v_visitor->m_sent_task_count);
    EXPECT_EQ(0u, v_visitor->m_total_requested_tasks);
    EXPECT_EQ(0u, v_visitor->m_total_thread_requests);
    EXPECT_DOUBLE_EQ(0.0, v_visitor->m_idle_time);
    EXPECT_DOUBLE_EQ(0.0, v_visitor->m_elapsed_time);
}

TEST(default_mpi_stats_visitor_test, consistent_with_multiple_visits) {
    gempba::default_mpi_stats v_stats(99);
    v_stats.m_received_task_count = 5;
    v_stats.m_sent_task_count = 15;
    v_stats.m_total_requested_tasks = 25;
    v_stats.m_total_thread_requests = 35;
    v_stats.m_idle_time = 0.5;
    v_stats.m_elapsed_time = 1.5;

    const auto v_visitor = std::make_unique<gempba::default_mpi_stats_visitor>();

    // first visit
    v_stats.visit(v_visitor.get());
    EXPECT_EQ(5u, v_visitor->m_received_task_count);

    // mutate stats and visit again
    v_stats.m_received_task_count = 50;
    v_stats.m_idle_time = 5.0;
    v_stats.visit(v_visitor.get());

    EXPECT_EQ(50u, v_visitor->m_received_task_count);
    EXPECT_DOUBLE_EQ(5.0, v_visitor->m_idle_time);
}
