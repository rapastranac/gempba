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
 *
 * Tests for the gempba_scheduler_* surface.
 *
 * The C ABI's job here is to translate plain C calls into virtual dispatches
 * on a gempba::scheduler instance held inside the wrapper struct.  Full
 * scheduler behaviour requires a live MPI process group, but the translation
 * itself does not — we build a scheduler_mock against the gempba::scheduler
 * interface and verify each C ABI entry point fires the right virtual with
 * the right argument transformation.
 */

#include <any>
#include <functional>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <vector>

#include <cabi/internals.hpp>
#include <cabi_test_fixture.hpp>
#include <gempba/stats/stats.hpp>
#include <gempba/stats/stats_visitor.hpp>

namespace gempba::cabi_tests {

    using ::testing::ByMove;
    using ::testing::Return;

    class cabi_scheduler_test : public cabi_fixture {};

    // ─── No-handle / null-arg guards ────────────────────────────────────────

    TEST_F(cabi_scheduler_test, create_null_out_returns_invalid_arg) {
        EXPECT_EQ(gempba_scheduler_create(GEMPBA_TOPOLOGY_CENTRALIZED, /*timeout=*/0.0, /*out=*/nullptr), GEMPBA_ERR_INVALID_ARG);
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }

    TEST_F(cabi_scheduler_test, destroy_null_is_safe) {
        gempba_scheduler_destroy(nullptr);
        SUCCEED();
    }

    // ─── GMock-backed dispatch tests ────────────────────────────────────────
    //
    // We don't test that gempba's real scheduler works (that's the C++ test
    // suite's job).  We test that the C ABI correctly:
    //   - resolves the wrapper handle to the underlying gempba::scheduler*
    //   - invokes the right virtual method
    //   - translates argument enums (gempba_goal_t / gempba_score_type_t) into
    //     the matching gempba::goal / gempba::score_type values
    //   - returns the right type-cast of the C++ result

    class scheduler_mock : public gempba::scheduler {
    public:
        // scheduler_traits
        MOCK_METHOD(void, barrier, (), (override));
        MOCK_METHOD(int, rank_me, (), (const, override));
        MOCK_METHOD(int, world_size, (), (const, override));
        MOCK_METHOD(std::unique_ptr<gempba::stats>, get_stats, (), (const, override));

        // scheduler
        MOCK_METHOD(double, elapsed_time, (), (const, override));
        MOCK_METHOD(std::size_t, get_pending_request_count, (), (const, override));
        MOCK_METHOD(void, set_goal, (gempba::goal, gempba::score_type), (override));
        MOCK_METHOD(void, set_custom_initial_topology, (tree&&), (override));
        MOCK_METHOD(std::vector<std::unique_ptr<gempba::stats>>, get_stats_vector, (), (const, override));
        MOCK_METHOD(void, synchronize_stats, (), (override));
        MOCK_METHOD(gempba::scheduler::center&, center_view, (), (override));
        MOCK_METHOD(gempba::scheduler::worker&, worker_view, (), (override));
    };

    /** Wraps a scheduler_mock in the C ABI handle shape so tests can call the C entry points. */
    static gempba_scheduler_t handle_for(scheduler_mock& p_mock, gempba_scheduler_s& p_storage) {
        p_storage.cpp = &p_mock;
        return reinterpret_cast<gempba_scheduler_t>(&p_storage);
    }

    TEST_F(cabi_scheduler_test, barrier_dispatches_to_underlying_scheduler) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, barrier()).Times(1);
        gempba_scheduler_barrier(handle_for(v_mock, v_s));
    }

    TEST_F(cabi_scheduler_test, rank_me_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, rank_me()).WillOnce(Return(7));
        EXPECT_EQ(gempba_scheduler_rank_me(handle_for(v_mock, v_s)), 7);
    }

    TEST_F(cabi_scheduler_test, world_size_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, world_size()).WillOnce(Return(4));
        EXPECT_EQ(gempba_scheduler_world_size(handle_for(v_mock, v_s)), 4);
    }

    TEST_F(cabi_scheduler_test, elapsed_time_returns_underlying_value) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, elapsed_time()).WillOnce(Return(12.5));
        EXPECT_DOUBLE_EQ(gempba_scheduler_elapsed_time(handle_for(v_mock, v_s)), 12.5);
    }

    TEST_F(cabi_scheduler_test, synchronize_stats_dispatches_to_underlying_scheduler) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, synchronize_stats()).Times(1);
        gempba_scheduler_synchronize_stats(handle_for(v_mock, v_s));
    }

    TEST_F(cabi_scheduler_test, set_goal_translates_enums_to_underlying_types) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, set_goal(gempba::MAXIMISE, gempba::score_type::I32)).Times(1);
        gempba_scheduler_set_goal(handle_for(v_mock, v_s), GEMPBA_GOAL_MAXIMISE, GEMPBA_SCORE_I32);

        EXPECT_CALL(v_mock, set_goal(gempba::MINIMISE, gempba::score_type::F64)).Times(1);
        gempba_scheduler_set_goal(handle_for(v_mock, v_s), GEMPBA_GOAL_MINIMISE, GEMPBA_SCORE_F64);
    }

    // ─── visit_stats ────────────────────────────────────────────────────────
    //
    // The cabi entry point pulls the stats vector and drives stats::visit() per
    // rank, mapping each std::any payload to a tagged gempba_stat_value_t.  We
    // mock stats to drive the lambda with one value of each type the cabi knows
    // how to translate.

    class scheduler_stats_mock : public gempba::stats {
    public:
        MOCK_METHOD(gempba::task_packet, serialize, (), (const, override));
        MOCK_METHOD(std::vector<std::string>, labels, (), (const, override));
        MOCK_METHOD(void, visit, ((std::function<void(const std::string&, std::any&&)>) ), (const, override));
        MOCK_METHOD(void, visit, (gempba::stats_visitor*), (const, override));
    };

    TEST_F(cabi_scheduler_test, visit_stats_null_visitor_is_safe) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        EXPECT_CALL(v_mock, get_stats_vector()).Times(0);
        gempba_scheduler_visit_stats(handle_for(v_mock, v_s), nullptr, nullptr);
        SUCCEED();
    }

    TEST_F(cabi_scheduler_test, visit_stats_dispatches_one_callback_per_rank_and_label) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;

        auto v_stats0 = std::make_unique<scheduler_stats_mock>();
        auto v_stats1 = std::make_unique<scheduler_stats_mock>();
        auto* v_s0 = v_stats0.get();
        auto* v_s1 = v_stats1.get();

        EXPECT_CALL(*v_s0, visit(::testing::A<std::function<void(const std::string&, std::any&&)>>())).WillOnce(::testing::Invoke([](const auto& p_v) {
            p_v(std::string{"count"}, std::any{int{42}});
            p_v(std::string{"ratio"}, std::any{double{3.14}});
        }));
        EXPECT_CALL(*v_s1, visit(::testing::A<std::function<void(const std::string&, std::any&&)>>())).WillOnce(::testing::Invoke([](const auto& p_v) {
            p_v(std::string{"size"}, std::any{std::size_t{100}});
        }));

        std::vector<std::unique_ptr<gempba::stats>> v_vec;
        v_vec.push_back(std::move(v_stats0));
        v_vec.push_back(std::move(v_stats1));
        EXPECT_CALL(v_mock, get_stats_vector()).WillOnce(Return(ByMove(std::move(v_vec))));

        struct row {
            int32_t m_rank;
            std::string m_label;
            gempba_stat_kind_t m_kind;
            int64_t m_i;
            double m_d;
        };
        std::vector<row> v_collected;

        gempba_scheduler_visit_stats(
                handle_for(v_mock, v_s),
                [](void* p_ud, int32_t p_rank, const char* p_label, const gempba_stat_value_t* p_val) {
                    auto* v_out = static_cast<std::vector<row>*>(p_ud);
                    row v_r{p_rank, std::string{p_label}, p_val->kind, 0, 0.0};
                    if (p_val->kind == GEMPBA_STAT_INT32) {
                        v_r.m_i = p_val->v.i32;
                    } else if (p_val->kind == GEMPBA_STAT_INT64) {
                        v_r.m_i = p_val->v.i64;
                    } else {
                        v_r.m_d = p_val->v.f64;
                    }
                    v_out->push_back(v_r);
                },
                &v_collected);

        ASSERT_EQ(v_collected.size(), 3u);
        EXPECT_EQ(v_collected[0].m_rank, 0);
        EXPECT_EQ(v_collected[0].m_label, "count");
        EXPECT_EQ(v_collected[0].m_kind, GEMPBA_STAT_INT32);
        EXPECT_EQ(v_collected[0].m_i, 42);
        EXPECT_EQ(v_collected[1].m_rank, 0);
        EXPECT_EQ(v_collected[1].m_label, "ratio");
        EXPECT_EQ(v_collected[1].m_kind, GEMPBA_STAT_DOUBLE);
        EXPECT_DOUBLE_EQ(v_collected[1].m_d, 3.14);
        EXPECT_EQ(v_collected[2].m_rank, 1);
        EXPECT_EQ(v_collected[2].m_label, "size");
        EXPECT_EQ(v_collected[2].m_kind, GEMPBA_STAT_INT64);
        EXPECT_EQ(v_collected[2].m_i, 100);
    }

    TEST_F(cabi_scheduler_test, visit_stats_translates_each_scalar_kind) {
        scheduler_mock v_mock;
        gempba_scheduler_s v_s;
        auto v_stats = std::make_unique<scheduler_stats_mock>();
        auto* v_raw = v_stats.get();

        EXPECT_CALL(*v_raw, visit(::testing::A<std::function<void(const std::string&, std::any&&)>>())).WillOnce(::testing::Invoke([](const auto& p_v) {
            p_v(std::string{"d"}, std::any{double{1.5}});
            p_v(std::string{"f"}, std::any{float{2.5f}});
            p_v(std::string{"i"}, std::any{int{-7}});
            p_v(std::string{"z"}, std::any{std::size_t{99}});
            p_v(std::string{"l"}, std::any{long{-3}});
            p_v(std::string{"b"}, std::any{true});
            p_v(std::string{"x"}, std::any{std::string{"unknown"}}); // skipped by cabi
        }));

        std::vector<std::unique_ptr<gempba::stats>> v_vec;
        v_vec.push_back(std::move(v_stats));
        EXPECT_CALL(v_mock, get_stats_vector()).WillOnce(Return(ByMove(std::move(v_vec))));

        struct row {
            std::string m_label;
            gempba_stat_kind_t m_kind;
            int64_t m_i;
            double m_d;
        };
        std::vector<row> v_collected;
        gempba_scheduler_visit_stats(
                handle_for(v_mock, v_s),
                [](void* p_ud, int32_t, const char* p_label, const gempba_stat_value_t* p_val) {
                    auto* v_out = static_cast<std::vector<row>*>(p_ud);
                    row v_r{std::string{p_label}, p_val->kind, 0, 0.0};
                    if (p_val->kind == GEMPBA_STAT_INT32) {
                        v_r.m_i = p_val->v.i32;
                    } else if (p_val->kind == GEMPBA_STAT_INT64) {
                        v_r.m_i = p_val->v.i64;
                    } else {
                        v_r.m_d = p_val->v.f64;
                    }
                    v_out->push_back(v_r);
                },
                &v_collected);

        ASSERT_EQ(v_collected.size(), 6u); // "unknown" std::string is filtered out
        EXPECT_EQ(v_collected[0].m_kind, GEMPBA_STAT_DOUBLE);
        EXPECT_DOUBLE_EQ(v_collected[0].m_d, 1.5);
        EXPECT_EQ(v_collected[1].m_kind, GEMPBA_STAT_DOUBLE);
        EXPECT_DOUBLE_EQ(v_collected[1].m_d, 2.5); // float promoted
        EXPECT_EQ(v_collected[2].m_kind, GEMPBA_STAT_INT32);
        EXPECT_EQ(v_collected[2].m_i, -7);
        EXPECT_EQ(v_collected[3].m_kind, GEMPBA_STAT_INT64);
        EXPECT_EQ(v_collected[3].m_i, 99);
        EXPECT_EQ(v_collected[4].m_kind, GEMPBA_STAT_INT64);
        EXPECT_EQ(v_collected[4].m_i, -3);
        EXPECT_EQ(v_collected[5].m_kind, GEMPBA_STAT_INT32);
        EXPECT_EQ(v_collected[5].m_i, 1); // bool true → 1
    }

} // namespace gempba::cabi_tests
