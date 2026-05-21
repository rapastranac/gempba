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
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <optional>

#include <gempba/gempba.hpp>
#include <gempba/node_manager.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>

/**
 * @author Andrés Pastrana
 * @date 2026
 */
class scheduler_stub final : public gempba::scheduler {
    struct center_stub final : center {
        void barrier() override {}

        [[nodiscard]] int rank_me() const override { return 0; }
        [[nodiscard]] int world_size() const override { return 1; }
        [[nodiscard]] std::unique_ptr<gempba::stats> get_stats() const override { return nullptr; }

        void run(gempba::task_packet, int) override {}

        gempba::task_packet get_result() override { return gempba::task_packet::EMPTY; }
        std::vector<gempba::result> get_all_results() override { return {}; }
    };

    struct worker_stub final : worker {
        void barrier() override {}

        [[nodiscard]] int rank_me() const override { return 0; }
        [[nodiscard]] int world_size() const override { return 1; }
        [[nodiscard]] std::unique_ptr<gempba::stats> get_stats() const override { return nullptr; }

        void run(gempba::node_manager&, std::map<int, std::shared_ptr<gempba::serial_runnable>>) override {}

        unsigned int force_push(gempba::task_packet&&, int) override { return 0; }
        [[nodiscard]] unsigned int next_process() const override { return 0; }
        std::optional<gempba::transmission_guard> try_open_transmission_channel() override { return std::nullopt; }
    };

    center_stub m_center;
    worker_stub m_worker;

public:
    void barrier() override {}

    [[nodiscard]] int rank_me() const override { return 0; }
    [[nodiscard]] int world_size() const override { return 1; }
    [[nodiscard]] std::unique_ptr<gempba::stats> get_stats() const override { return nullptr; }
    [[nodiscard]] double elapsed_time() const override { return 0.0; }

    void set_goal(gempba::goal, gempba::score_type) override {}

    void set_custom_initial_topology(tree&&) override {}

    [[nodiscard]] std::vector<std::unique_ptr<gempba::stats>> get_stats_vector() const override { return {}; }

    void synchronize_stats() override {}

    center& center_view() override { return m_center; }
    worker& worker_view() override { return m_worker; }
};


class gempba_test : public ::testing::Test {
protected:
    void TearDown() override {
        gempba::reset_node_manager();
        gempba::reset_load_balancer();
        gempba::reset_scheduler();
    }
};


TEST_F(gempba_test, get_load_balancer_returns_nullptr_when_not_created) { EXPECT_EQ(nullptr, gempba::get_load_balancer()); }

TEST_F(gempba_test, mt_create_load_balancer_quasi_horizontal) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    ASSERT_NE(nullptr, v_lb);
    EXPECT_EQ(gempba::QUASI_HORIZONTAL, v_lb->get_balancing_policy());
}

TEST_F(gempba_test, mt_create_load_balancer_work_stealing) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::WORK_STEALING);
    ASSERT_NE(nullptr, v_lb);
    EXPECT_EQ(gempba::WORK_STEALING, v_lb->get_balancing_policy());
}

TEST_F(gempba_test, mt_create_load_balancer_throws_if_already_exists) {
    gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    EXPECT_THROW(gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL), std::runtime_error);
}

TEST_F(gempba_test, mt_create_load_balancer_custom_implementation) {
    auto v_custom = std::make_unique<gempba::work_stealing_load_balancer>();
    const gempba::work_stealing_load_balancer* v_raw = v_custom.get();
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(std::move(v_custom));
    ASSERT_NE(nullptr, v_lb);
    EXPECT_EQ(v_raw, v_lb);
}

TEST_F(gempba_test, mt_create_load_balancer_custom_throws_if_already_exists) {
    gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    auto v_custom = std::make_unique<gempba::work_stealing_load_balancer>();
    EXPECT_THROW(gempba::mt::create_load_balancer(std::move(v_custom)), std::runtime_error);
}

TEST_F(gempba_test, get_load_balancer_returns_non_null_after_creation) {
    gempba::mt::create_load_balancer(gempba::WORK_STEALING);
    EXPECT_NE(nullptr, gempba::get_load_balancer());
}

TEST_F(gempba_test, reset_load_balancer_clears_instance) {
    gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    ASSERT_NE(nullptr, gempba::get_load_balancer());
    gempba::reset_load_balancer();
    EXPECT_EQ(nullptr, gempba::get_load_balancer());
}

TEST_F(gempba_test, mt_create_node_manager) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    gempba::node_manager& v_nm = gempba::mt::create_node_manager(v_lb);
    EXPECT_EQ(&v_nm, &gempba::get_node_manager());
}

TEST_F(gempba_test, mt_create_node_manager_throws_if_already_exists) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    gempba::mt::create_node_manager(v_lb);
    EXPECT_THROW(gempba::mt::create_node_manager(v_lb), std::runtime_error);
}

TEST_F(gempba_test, get_node_manager_throws_when_not_instantiated) { EXPECT_THROW([[maybe_unused]] auto& v_ref = gempba::get_node_manager(), std::runtime_error); }

TEST_F(gempba_test, reset_node_manager_clears_instance) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    gempba::mt::create_node_manager(v_lb);
    gempba::reset_node_manager();
    EXPECT_THROW([[maybe_unused]] auto& v_ref = gempba::get_node_manager(), std::runtime_error);
}

TEST_F(gempba_test, get_scheduler_throws_when_not_instantiated) { EXPECT_THROW([[maybe_unused]] auto* v_ptr = gempba::get_scheduler(), std::runtime_error); }

TEST_F(gempba_test, mp_create_scheduler_custom_implementation) {
    auto v_stub = std::make_unique<scheduler_stub>();
    const scheduler_stub* v_raw = v_stub.get();
    gempba::scheduler* v_sched = gempba::mp::create_scheduler(std::move(v_stub));
    ASSERT_NE(nullptr, v_sched);
    EXPECT_EQ(v_raw, v_sched);
    EXPECT_EQ(v_sched, gempba::get_scheduler());
}

TEST_F(gempba_test, mp_create_scheduler_throws_if_already_exists) {
    gempba::mp::create_scheduler(std::make_unique<scheduler_stub>());
    EXPECT_THROW(gempba::mp::create_scheduler(std::make_unique<scheduler_stub>()), std::runtime_error);
}

TEST_F(gempba_test, reset_scheduler_clears_instance) {
    gempba::mp::create_scheduler(std::make_unique<scheduler_stub>());
    ASSERT_NE(nullptr, gempba::get_scheduler());
    gempba::reset_scheduler();
    EXPECT_THROW([[maybe_unused]] auto* v_ptr = gempba::get_scheduler(), std::runtime_error);
}

TEST_F(gempba_test, mp_create_load_balancer_quasi_horizontal) {
    gempba::load_balancer* v_lb = gempba::mp::create_load_balancer(gempba::QUASI_HORIZONTAL, nullptr);
    ASSERT_NE(nullptr, v_lb);
    EXPECT_EQ(gempba::QUASI_HORIZONTAL, v_lb->get_balancing_policy());
}

TEST_F(gempba_test, mp_create_load_balancer_work_stealing) {
    gempba::load_balancer* v_lb = gempba::mp::create_load_balancer(gempba::WORK_STEALING, nullptr);
    ASSERT_NE(nullptr, v_lb);
    EXPECT_EQ(gempba::WORK_STEALING, v_lb->get_balancing_policy());
}

TEST_F(gempba_test, mp_create_load_balancer_throws_if_already_exists) {
    gempba::mp::create_load_balancer(gempba::QUASI_HORIZONTAL, nullptr);
    EXPECT_THROW(gempba::mp::create_load_balancer(gempba::WORK_STEALING, nullptr), std::runtime_error);
}

TEST_F(gempba_test, mp_create_load_balancer_custom_implementation) {
    auto v_custom = std::make_unique<gempba::quasi_horizontal_load_balancer>();
    const gempba::quasi_horizontal_load_balancer* v_raw = v_custom.get();
    gempba::load_balancer* v_lb = gempba::mp::create_load_balancer(std::move(v_custom));
    EXPECT_EQ(v_raw, v_lb);
}

TEST_F(gempba_test, mp_create_node_manager) {
    gempba::load_balancer* v_lb = gempba::mp::create_load_balancer(gempba::QUASI_HORIZONTAL, nullptr);
    gempba::node_manager& v_nm = gempba::mp::create_node_manager(v_lb, nullptr);
    EXPECT_EQ(&v_nm, &gempba::get_node_manager());
}

TEST_F(gempba_test, mp_create_node_manager_throws_if_already_exists) {
    gempba::load_balancer* v_lb = gempba::mp::create_load_balancer(gempba::QUASI_HORIZONTAL, nullptr);
    gempba::mp::create_node_manager(v_lb, nullptr);
    EXPECT_THROW(gempba::mp::create_node_manager(v_lb, nullptr), std::runtime_error);
}

TEST_F(gempba_test, mp_get_default_mpi_stats_visitor_returns_non_null) {
    auto v_visitor = gempba::mp::get_default_mpi_stats_visitor();
    ASSERT_NE(nullptr, v_visitor);
}

TEST_F(gempba_test, shutdown_resets_all_singletons) {
    gempba::load_balancer* v_lb = gempba::mt::create_load_balancer(gempba::QUASI_HORIZONTAL);
    gempba::mt::create_node_manager(v_lb);
    gempba::mp::create_scheduler(std::make_unique<scheduler_stub>());

    const int v_ret = gempba::shutdown();

    EXPECT_EQ(EXIT_SUCCESS, v_ret);
    EXPECT_EQ(nullptr, gempba::get_load_balancer());
    EXPECT_THROW([[maybe_unused]] auto& v_ref = gempba::get_node_manager(), std::runtime_error);
    EXPECT_THROW([[maybe_unused]] auto* v_ptr = gempba::get_scheduler(), std::runtime_error);
}

TEST_F(gempba_test, check_not_null_throws_for_default_constructed_node) {
    const gempba::node v_null_node;
    EXPECT_THROW(gempba::check_not_null(v_null_node), std::runtime_error);
}

TEST_F(gempba_test, check_not_null_does_not_throw_for_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    const gempba::node v_valid_node = gempba::create_dummy_node(v_lb);
    EXPECT_NO_THROW(gempba::check_not_null(v_valid_node));
}

namespace {
    class minimal_node_core final : public gempba::node_core {
    public:
        void run() override {}
        void delegate_locally(gempba::load_balancer*) override {}
        void delegate_remotely(gempba::scheduler::worker*, int) override {}
        void set_result(const gempba::task_packet&) override {}
        gempba::task_packet get_result() override { return gempba::task_packet::EMPTY; }
        gempba::task_packet serialize() override { return gempba::task_packet::EMPTY; }
        void deserialize(const gempba::task_packet&) override {}
        void set_result_serializer(const std::function<gempba::task_packet(std::any)>&) override {}
        void set_result_deserializer(const std::function<std::any(gempba::task_packet)>&) override {}
        [[nodiscard]] bool is_dummy() const override { return false; }
        [[nodiscard]] std::thread::id get_thread_id() const override { return {}; }
        [[nodiscard]] int get_node_id() const override { return 0; }
        [[nodiscard]] int get_forward_count() const override { return 0; }
        [[nodiscard]] int get_push_count() const override { return 0; }
        [[nodiscard]] gempba::node_state get_state() const override { return gempba::UNUSED; }
        void set_state(gempba::node_state) override {}
        [[nodiscard]] bool is_consumed() const override { return false; }
        [[nodiscard]] std::shared_ptr<node_core> get_root() override { return nullptr; }
        void set_parent(const std::shared_ptr<node_core>&) override {}
        [[nodiscard]] std::shared_ptr<node_core> get_parent() override { return nullptr; }
        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_child() override { return nullptr; }
        [[nodiscard]] std::shared_ptr<node_core> get_second_leftmost_child() override { return nullptr; }
        void remove_leftmost_child() override {}
        void remove_second_leftmost_child() override {}
        [[nodiscard]] std::shared_ptr<node_core> get_leftmost_sibling() override { return nullptr; }
        [[nodiscard]] std::shared_ptr<node_core> get_left_sibling() override { return nullptr; }
        [[nodiscard]] std::shared_ptr<node_core> get_right_sibling() override { return nullptr; }
        std::list<std::shared_ptr<node_core>> get_children() override { return {}; }
        [[nodiscard]] int get_children_count() const override { return 0; }
        void add_child(const std::shared_ptr<node_core>&) override {}
        std::any get_any_result() override { return {}; }
        [[nodiscard]] bool is_result_ready() const override { return false; }
        bool should_branch() override { return true; }
        void remove_child(std::shared_ptr<node_core>&) override {}
        void prune() override {}
    };
} // namespace

TEST_F(gempba_test, create_custom_node_wraps_user_provided_core) {
    auto v_custom_core = std::make_shared<minimal_node_core>();
    gempba::node v_node = gempba::create_custom_node(v_custom_core);

    EXPECT_NE(nullptr, v_node);
    EXPECT_TRUE(v_node.should_branch()); // forwarded to minimal_node_core::should_branch()
}

namespace {
    // Minimal load_balancer whose set_root throws. Used to drive the exception-propagation
    // branch of create_dummy_node so branch coverage hits both edges of the call.
    class throwing_balancer final : public gempba::load_balancer {
        unsigned int m_id_counter = 0;

    public:
        gempba::balancing_policy get_balancing_policy() override { return gempba::QUASI_HORIZONTAL; }
        unsigned int generate_unique_id() override { return ++m_id_counter; }
        [[nodiscard]] double get_idle_time() const override { return 0.0; }
        void set_root(std::thread::id, std::shared_ptr<gempba::node_core>&) override { throw std::runtime_error("set_root failed"); }
        std::shared_ptr<std::shared_ptr<gempba::node_core>> get_root(std::thread::id) override { return {}; }
        void set_thread_pool_size(unsigned int) override {}
        [[nodiscard]] unsigned int get_thread_pool_size() const override { return 0; }
        [[nodiscard]] std::size_t get_tasks_running_count() const override { return 0; }
        std::future<std::any> force_local_submit(std::function<std::any()>&&) override { return {}; }
        void forward(gempba::node&) override {}
        bool try_local_submit(gempba::node&) override { return false; }
        bool try_remote_submit(gempba::node&, int) override { return false; }
        void wait() override {}
        [[nodiscard]] bool is_done() const override { return true; }
        [[nodiscard]] std::size_t get_thread_request_count() const override { return 0; }
    };
} // namespace

TEST_F(gempba_test, create_dummy_node_propagates_balancer_exception) {
    throwing_balancer v_lb;
    EXPECT_THROW(gempba::create_dummy_node(v_lb), std::runtime_error);
}

TEST_F(gempba_test, create_seed_node_constructs_a_node_with_no_parent) {
    gempba::work_stealing_load_balancer v_lb;
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    const gempba::node v_seed = gempba::create_seed_node<void, int>(v_lb, v_fn, std::make_tuple(7));
    EXPECT_NE(nullptr, v_seed);
}

TEST_F(gempba_test, mt_create_explicit_node_builds_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    gempba::node v_parent = gempba::create_dummy_node(v_lb);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    const gempba::node v_node = gempba::mt::create_explicit_node<void, int>(v_lb, v_parent, v_fn, std::make_tuple(7));
    EXPECT_NE(nullptr, v_node);
}

TEST_F(gempba_test, mt_create_lazy_node_builds_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    const gempba::node v_parent = gempba::create_dummy_node(v_lb);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    std::function<std::optional<std::tuple<int>>()> v_init = [] { return std::make_optional(std::make_tuple(7)); };
    const gempba::node v_node = gempba::mt::create_lazy_node<void, int>(v_lb, v_parent, v_fn, v_init);
    EXPECT_NE(nullptr, v_node);
}

TEST_F(gempba_test, mp_create_explicit_node_builds_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    const gempba::node v_parent = gempba::create_dummy_node(v_lb);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    std::function<gempba::task_packet(int)> v_ser = [](int) { return gempba::task_packet::EMPTY; };
    std::function<std::tuple<int>(gempba::task_packet)> v_deser = [](const gempba::task_packet&) { return std::make_tuple(0); };
    const gempba::node v_node = gempba::mp::create_explicit_node<void, int>(v_lb, v_parent, v_fn, std::make_tuple(7), v_ser, v_deser);
    EXPECT_NE(nullptr, v_node);
}

TEST_F(gempba_test, mp_create_lazy_node_builds_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    const gempba::node v_parent = gempba::create_dummy_node(v_lb);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    std::function<std::optional<std::tuple<int>>()> v_init = [] { return std::make_optional(std::make_tuple(7)); };
    std::function<gempba::task_packet(int)> v_ser = [](int) { return gempba::task_packet::EMPTY; };
    std::function<std::tuple<int>(gempba::task_packet)> v_deser = [](const gempba::task_packet&) { return std::make_tuple(0); };
    const gempba::node v_node = gempba::mp::create_lazy_node<void, int>(v_lb, v_parent, v_fn, v_init, v_ser, v_deser);
    EXPECT_NE(nullptr, v_node);
}

TEST_F(gempba_test, mp_create_from_bytes_node_builds_valid_node) {
    gempba::work_stealing_load_balancer v_lb;
    const gempba::node v_parent = gempba::create_dummy_node(v_lb);
    std::function<void(std::thread::id, int, gempba::node)> v_fn = [](std::thread::id, int, const gempba::node&) {};
    std::function<gempba::task_packet(int)> v_ser = [](int) { return gempba::task_packet::EMPTY; };
    std::function<std::tuple<int>(gempba::task_packet)> v_deser = [](const gempba::task_packet&) { return std::make_tuple(0); };
    const gempba::node v_node = gempba::mp::create_from_bytes_node<void, int>(v_lb, v_parent, v_fn, v_ser, v_deser);
    EXPECT_NE(nullptr, v_node);
}

TEST_F(gempba_test, mp_runnables_return_none_create_builds_serial_runnable) {
    auto v_invokable = [](std::thread::id, int, const gempba::node&) {};
    const std::function<std::tuple<int>(const gempba::task_packet&&)> v_deser = [](const gempba::task_packet&&) { return std::make_tuple(0); };
    const std::shared_ptr<gempba::serial_runnable> v_runnable = gempba::mp::runnables::return_none::create<int>(1, v_invokable, v_deser);
    EXPECT_NE(nullptr, v_runnable);
}

TEST_F(gempba_test, mp_runnables_return_value_create_builds_serial_runnable) {
    auto v_invokable = [](std::thread::id, int, const gempba::node&) -> int { return 0; };
    const std::function<std::tuple<int>(const gempba::task_packet&&)> v_deser = [](const gempba::task_packet&&) { return std::make_tuple(0); };
    const std::function<gempba::task_packet(int)> v_result_ser = [](int) { return gempba::task_packet::EMPTY; };
    const std::shared_ptr<gempba::serial_runnable> v_runnable = gempba::mp::runnables::return_value::create<int, int>(1, v_invokable, v_deser, v_result_ser);
    EXPECT_NE(nullptr, v_runnable);
}
