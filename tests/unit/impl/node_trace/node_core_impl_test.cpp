/*
 * MIT License
 *
 * Copyright (c) 2024. Andrés Pastrana
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

#include <functional>
#include <future>
#include <test_utils.hpp>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <gempba/node_manager.hpp>
#include <gempba/core/load_balancer.hpp>
#include <impl/nodes/node_factory.hpp>


/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */

class load_balancer_mock final : public gempba::load_balancer {
public:
    MOCK_METHOD(gempba::balancing_policy, get_balancing_policy, (), (override));
    MOCK_METHOD(unsigned int, generate_unique_id, (), (override));
    MOCK_METHOD(double, get_idle_time, (), (const override));
    MOCK_METHOD(void, set_root, (const std::thread::id thread_id, std::shared_ptr<gempba::node_core>& root), (override));
    MOCK_METHOD(std::shared_ptr<std::shared_ptr<gempba::node_core>>, get_root, (const std::thread::id thread_id), (override));
    MOCK_METHOD(void, set_thread_pool_size, (unsigned p_size), (override));
    MOCK_METHOD(std::future<std::any>, force_local_submit, (std::function<std::any()> &&p_function), (override));
    MOCK_METHOD(void, forward, (gempba::node &p_node), (override));
    MOCK_METHOD(bool, try_local_submit, (gempba::node &p_node), (override));
    MOCK_METHOD(bool, try_remote_submit, (gempba::node &p_node, int p_runnable_id), (override));
    MOCK_METHOD(void, wait, (), (override));
    MOCK_METHOD(bool, is_done, (), (const ,override));
    MOCK_METHOD(std::size_t, get_thread_request_count, (), ( const, override));
};

class my_struct {
    int m_value;

public:
    friend class boost::serialization::access;

    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    template<class Archive>
    void serialize(Archive &p_ar, const unsigned int p_version) {
        p_ar & m_value;
    }


    my_struct() :
        m_value(0) {
    }

    explicit my_struct(const int p_value) :
        m_value(p_value) {
    }

    my_struct(const my_struct &p_other) = default;

    my_struct(my_struct &&p_other) noexcept :
        m_value(std::exchange(p_other.m_value, 0)) {
    }

    bool operator==(const my_struct &p_other) const { return m_value == p_other.m_value; }

    my_struct &operator=(my_struct &&p_other) noexcept {
        if (this != &p_other) {
            m_value = std::exchange(p_other.m_value, 0);
        }
        return *this;
    }
};

gempba::node foo(gempba::load_balancer &p_balancer, auto p_dummy_function) {
    my_struct v_ins{7};
    float v_f1 = 1.0f;
    double v_d1 = 3.0;
    std::vector<float> v_vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

    auto v_dummy = gempba::node();
    return gempba::node_factory::create_explicit_node<void>(p_balancer, v_dummy, p_dummy_function, std::make_tuple(v_ins, v_f1, v_d1, v_vec));
}

class node_core_impl_test : public ::testing::Test {
protected:
    load_balancer_mock m_balancer_mock;
    std::map<std::thread::id, std::shared_ptr<std::shared_ptr<gempba::node_core> > > m_roots;
    unsigned int m_unique_id = 0;

    void SetUp() override {

        EXPECT_CALL(m_balancer_mock, get_balancing_policy()).WillRepeatedly([]() { return gempba::balancing_policy::QUASI_HORIZONTAL; });
        EXPECT_CALL(m_balancer_mock, generate_unique_id()).WillRepeatedly([this]() { return ++m_unique_id; });
        EXPECT_CALL(m_balancer_mock, set_root(testing::_, testing::_)).WillRepeatedly([this](const std::thread::id p_id, std::shared_ptr<gempba::node_core> &p_node) {
            m_roots.emplace(p_id, std::make_shared<std::shared_ptr<gempba::node_core> >(p_node));
        });
        EXPECT_CALL(m_balancer_mock, get_root(testing::_)).WillRepeatedly([this](const std::thread::id p_id) { return m_roots[p_id]; });
        EXPECT_CALL(m_balancer_mock, try_remote_submit(testing::_, testing::_)).WillRepeatedly([](gempba::node &p_node, int) {
            p_node.prune();
            p_node.set_state(gempba::SENT_TO_ANOTHER_PROCESS);
            return false;
        });
    }

    void TearDown() override { m_roots.clear(); }
};

TEST_F(node_core_impl_test, explicit_initialization) {
    my_struct object;
    float f_val;
    double d_val;
    std::vector<float> v_vec;

    auto v_dummy_function = [&](std::thread::id, my_struct p_my_struct, float p_f, double p_d, std::vector<float> p_v, gempba::node) {
        object = std::move(p_my_struct);
        f_val = p_f;
        d_val = p_d;
        v_vec = std::move(p_v);
    };

    gempba::node v_node = foo(m_balancer_mock, v_dummy_function);

    v_node.run();

    const auto exp_struct = my_struct{7};
    ASSERT_EQ(exp_struct, object);
    ASSERT_FLOAT_EQ(1.0f, f_val);
    ASSERT_DOUBLE_EQ(3.0, d_val);

    std::vector v_exp_vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    ASSERT_EQ(10, v_vec.size());
    ASSERT_EQ(v_exp_vec, v_vec);
    ASSERT_EQ(gempba::FORWARDED, v_node.get_state());
}

TEST_F(node_core_impl_test, lazily_initialization) {
    my_struct v_object;
    float v_float;
    double v_double;
    std::vector<float> v_vec;

    std::function<void(std::thread::id, my_struct, float, double, std::vector<float>, gempba::node)> dummy_function = [&](std::thread::id, my_struct p_my_struct, const float p_f,
                                                                                                                          double p_d, std::vector<float> p_v, gempba::node) {
        v_object = std::move(p_my_struct);
        v_float = p_f;
        v_double = p_d;
        v_vec = std::move(p_v);
    };

    std::function<std::optional<std::tuple<my_struct, float, double, std::vector<float> > >()> v_initializer = []() {
        my_struct v_ins{7};
        float v_f1 = 1.0f;
        double v_d1 = 3.0;
        std::vector v_vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};

        return std::make_tuple(v_ins, v_f1, v_d1, v_vec);
    };

    gempba::node v_node = gempba::node_factory::create_lazy_node<void>(m_balancer_mock, gempba::node(nullptr), dummy_function, v_initializer);
    v_node.run();

    const auto exp_struct = my_struct{7};
    ASSERT_EQ(exp_struct, v_object);
    ASSERT_FLOAT_EQ(1.0f, v_float);
    ASSERT_DOUBLE_EQ(3.0, v_double);

    const std::vector exp_vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    ASSERT_EQ(10, v_vec.size());
    ASSERT_EQ(exp_vec, v_vec);
    ASSERT_EQ(gempba::FORWARDED, v_node.get_state());
}

TEST_F(node_core_impl_test, serializable_void) {
    my_struct actual_struct;
    float actual_float;
    double actual_double;
    std::vector<float> actual_vector;

    std::function<void(std::thread::id, my_struct, float, double, std::vector<float>, gempba::node)> dummy_function = [&](std::thread::id, my_struct p_my_struct, float p_f,
                                                                                                                          double p_d, std::vector<float> p_vec, gempba::node) {
        actual_struct = std::move(p_my_struct);
        actual_float = p_f;
        actual_double = p_d;
        actual_vector = std::move(p_vec);
    };


    std::function<gempba::task_packet(my_struct, float, double, std::vector<float>)> args_serializer = [](const my_struct &p_ins, float p_f_val, double p_d_val,
                                                                                                          const std::vector<float> &vec) {
        std::stringstream ss;
        boost::archive::text_oarchive archive(ss);
        archive << p_ins;
        archive << p_f_val;
        archive << p_d_val;
        archive << vec;
        return gempba::task_packet(ss.str());
    };

    std::function<std::tuple<my_struct, float, double, std::vector<float> >(gempba::task_packet)> args_deserializer = [](gempba::task_packet p_task) {
        my_struct ins;
        float f_val;
        double d_val;
        std::vector<float> vec;

        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));
        boost::archive::text_iarchive archive(v_ss);
        archive >> ins;
        archive >> f_val;
        archive >> d_val;
        archive >> vec;

        return std::make_tuple(ins, f_val, d_val, vec);
    };
    auto v_dummy = gempba::node();
    auto v_node = gempba::node_factory::create_serializable_node<void>(m_balancer_mock, v_dummy, dummy_function, args_serializer, args_deserializer);

    try {
        v_node.run();
        FAIL();
    } catch (std::exception &e) {
        ASSERT_STREQ("node arguments have not been initialized", e.what());
    };

    const my_struct &expected_struct = my_struct{7};
    constexpr float expected_float = 1.0f;
    constexpr double expected_double = 3.0;
    const std::vector<float> &expected_vector = std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    const gempba::task_packet buffer = args_serializer(expected_struct, expected_float, expected_double, expected_vector);

    v_node.deserialize(gempba::task_packet(buffer));
    v_node.run();

    gempba::node_state state = v_node.get_state();
    ASSERT_EQ(gempba::node_state::FORWARDED, state);

    ASSERT_EQ(expected_struct, actual_struct);
    ASSERT_FLOAT_EQ(expected_float, actual_float);
    ASSERT_DOUBLE_EQ(expected_double, actual_double);
    ASSERT_EQ(expected_vector, actual_vector);
}

struct custom_object {
    my_struct m_my_struct;
    float m_f_value{};
    double m_d_value{};
    std::vector<float> m_vector;

    friend class boost::serialization::access;

    // When the class Archive corresponds to an output archive, the
    // & operator is defined similar to <<.  Likewise, when the class Archive
    // is a type of input archive the & operator is defined similar to >>.
    template<class Archive>
    void serialize(Archive &ar, const unsigned int version) {
        ar & m_my_struct;
        ar & m_f_value;
        ar & m_d_value;
        ar & m_vector;
    }


    custom_object() = default;

    explicit custom_object(my_struct p_my_struct, float p_f_value, double p_d_value, std::vector<float> &p_vector) :
        m_my_struct(std::move(p_my_struct)), m_f_value(p_f_value), m_d_value(p_d_value), m_vector(std::move(p_vector)) {
    }

    custom_object(const custom_object &p_other) = default;

    custom_object(custom_object &&p_other) noexcept {
        m_my_struct = std::exchange(p_other.m_my_struct, my_struct{});
        m_f_value = std::exchange(p_other.m_f_value, 0.0f);
        m_d_value = std::exchange(p_other.m_d_value, 0.0);
        m_vector = std::exchange(p_other.m_vector, std::vector<float>{});
    }

    bool operator==(const custom_object &p_other) const {
        return m_my_struct == p_other.m_my_struct && m_f_value == p_other.m_f_value && m_d_value == p_other.m_d_value && m_vector == p_other.m_vector;
    }

    custom_object &operator=(custom_object &&p_other) noexcept {
        if (this != &p_other) {
            m_my_struct = std::exchange(p_other.m_my_struct, my_struct{});
            m_f_value = std::exchange(p_other.m_f_value, 0.0f);
            m_d_value = std::exchange(p_other.m_d_value, 0.0);
            m_vector = std::exchange(p_other.m_vector, std::vector<float>{});
        }
        return *this;
    }
};

TEST_F(node_core_impl_test, serializable_non_void) {
    std::function<custom_object(std::thread::id, my_struct, float, double, std::vector<float>, gempba::node)> v_dummy_function =
            [](std::thread::id, const my_struct &p_my_struct, float p_f, double p_d, std::vector<float> p_v, gempba::node) { return custom_object{p_my_struct, p_f, p_d, p_v}; };


    std::function<gempba::task_packet(my_struct, float, double, std::vector<float>)> v_args_serializer = [](const my_struct &p_ins, float p_f_val, double p_d_val,
                                                                                                            std::vector<float> p_vec) {
        std::stringstream v_ss;
        boost::archive::text_oarchive v_archive(v_ss);
        v_archive << p_ins;
        v_archive << p_f_val;
        v_archive << p_d_val;
        v_archive << p_vec;
        return gempba::task_packet(v_ss.str());
    };

    std::function<std::tuple<my_struct, float, double, std::vector<float> >(gempba::task_packet)> v_deserializer = [](const gempba::task_packet &p_task) {
        my_struct v_ins;
        float v_f_val;
        double v_d_val;
        std::vector<float> v_vec;

        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));
        boost::archive::text_iarchive v_archive(v_ss);
        v_archive >> v_ins;
        v_archive >> v_f_val;
        v_archive >> v_d_val;
        v_archive >> v_vec;

        return std::make_tuple(v_ins, v_f_val, v_d_val, v_vec);
    };
    gempba::node v_node = gempba::node_factory::create_serializable_node<custom_object, my_struct, float, double, std::vector<float> >(
            m_balancer_mock, gempba::node(), v_dummy_function, v_args_serializer, v_deserializer);

    // act
    try {
        v_node.run();
        FAIL();
    } catch (std::exception &v_e) {
        ASSERT_STREQ("node arguments have not been initialized", v_e.what());
    };

    std::function<gempba::task_packet(std::any)> v_result_serializer = [](std::any p_result) {
        const auto v_object = std::any_cast<custom_object>(p_result);

        std::stringstream v_ss;
        boost::archive::text_oarchive v_archive(v_ss);
        v_archive << v_object;
        return gempba::task_packet(v_ss.str());
    };
    std::function<std::any(gempba::task_packet)> v_result_deserializer = [](gempba::task_packet p_task) -> std::any {
        custom_object v_object;
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));
        boost::archive::text_iarchive v_iarchive(v_ss);
        v_iarchive >> v_object;

        return {v_object};
    };

    v_node.set_result_serializer(v_result_serializer);
    v_node.set_result_deserializer(v_result_deserializer);


    const my_struct &v_expected_struct = my_struct{7};
    float v_expected_float = 1.0f;
    double v_expected_double = 3.0;
    const std::vector<float> &v_expected_vector = std::vector<float>{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    gempba::task_packet v_buffer = v_args_serializer(v_expected_struct, v_expected_float, v_expected_double, v_expected_vector);

    v_node.deserialize(gempba::task_packet(v_buffer));
    v_node.run();

    std::any v_any_result = v_node.get_any_result();
    gempba::task_packet v_serialized_result = v_node.get_result();
    std::string v_serialized_result_str(reinterpret_cast<const char *>(v_serialized_result.data()), v_serialized_result.size());

    std::string v_expected = "22 serialization::archive 20 0 0 0 0 7 1.000000000e+00 3.00000000000000000e+00 10 0 1.000000000e+00 2.000000000e+00 3.000000000e+00 4.000000000e+00 "
            "5.000000000e+00 6.000000000e+00 7.000000000e+00 8.000000000e+00 9.000000000e+00 1.000000000e+01";
    ASSERT_EQ(gempba::test_utils::strip_boost_metadata(v_expected), gempba::test_utils::strip_boost_metadata(v_serialized_result_str));


    auto v_object = std::any_cast<custom_object>(v_any_result);

    gempba::node_state v_state = v_node.get_state();
    ASSERT_EQ(gempba::node_state::RETRIEVED, v_state);
    ASSERT_EQ(v_expected_struct, v_object.m_my_struct);
    ASSERT_FLOAT_EQ(v_expected_float, v_object.m_f_value);
    ASSERT_DOUBLE_EQ(v_expected_double, v_object.m_d_value);
    ASSERT_EQ(v_expected_vector, v_object.m_vector);
}

TEST_F(node_core_impl_test, remote_result_non_void) {
    std::function<custom_object(std::thread::id, my_struct, float, double, std::vector<float>, gempba::node)> v_dummy_function =
            [](std::thread::id, const my_struct &p_my_struct, float p_f, double p_d, std::vector<float> p_v, gempba::node) { return custom_object{p_my_struct, p_f, p_d, p_v}; };


    std::function<std::optional<std::tuple<my_struct, float, double, std::vector<float> > >()> v_args_initializer = [] {
        my_struct my_struct{7};
        float f_value = 1.0f;
        double d_value = 3.0;
        std::vector<float> vector{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
        return std::make_tuple(my_struct, f_value, d_value, vector);
    };
    std::function<gempba::task_packet(my_struct, float, double, std::vector<float>)> v_args_serializer = [](my_struct p_ins, float p_f_val, double p_d_val, std::vector<float> p_vec) {
        std::stringstream ss;
        boost::archive::text_oarchive archive(ss);
        archive << p_ins;
        archive << p_f_val;
        archive << p_d_val;
        archive << p_vec;
        return gempba::task_packet(ss.str());
    };
    std::function<std::tuple<my_struct, float, double, std::vector<float> >(gempba::task_packet)> v_args_deserializer = [](const gempba::task_packet &p_task) {
        my_struct ins;
        float f_val;
        double d_val;
        std::vector<float> vec;

        std::stringstream ss;
        ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));
        boost::archive::text_iarchive archive(ss);
        archive >> ins;
        archive >> f_val;
        archive >> d_val;
        archive >> vec;

        return std::make_tuple(ins, f_val, d_val, vec);
    };

    gempba::node v_node = gempba::node_factory::create_serializable_lazy_node<custom_object, my_struct, float, double, std::vector<float> >(
            m_balancer_mock, gempba::node(), v_dummy_function, v_args_initializer, v_args_serializer, v_args_deserializer);


    std::function<gempba::task_packet(std::any)> v_result_serializer = [](std::any p_result) {
        const auto v_object = std::any_cast<custom_object>(p_result);

        std::stringstream v_ss;
        boost::archive::text_oarchive v_archive(v_ss);
        v_archive << v_object;
        return gempba::task_packet(v_ss.str());
    };
    std::function<std::any(gempba::task_packet)> v_result_deserializer = [](gempba::task_packet p_task) -> std::any {
        custom_object v_object;
        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));

        boost::archive::text_iarchive v_iarchive(v_ss);
        v_iarchive >> v_object;

        return {v_object};
    };

    v_node.set_result_serializer(v_result_serializer);
    v_node.set_result_deserializer(v_result_deserializer);

    gempba::node_manager v_node_manager(&m_balancer_mock, nullptr);
    ASSERT_EQ(v_node, v_node.get_root());
    ASSERT_EQ(nullptr, v_node.get_parent());

    constexpr int runner_id = -1;
    bool v_is_submitted = v_node_manager.try_remote_submit(v_node, runner_id); // mimics a remote call
    ASSERT_FALSE(v_is_submitted);

    // It should be pruned
    ASSERT_EQ(nullptr, v_node.get_root());
    ASSERT_EQ(nullptr, v_node.get_parent());

    try {
        v_node.run();
        FAIL();
    } catch (std::exception &e) {
        ASSERT_STREQ("node is already consumed, node: 1, state: SENT_TO_ANOTHER_PROCESS", e.what());
    };
    {
        gempba::node_state v_state = v_node.get_state();
        ASSERT_EQ(gempba::node_state::SENT_TO_ANOTHER_PROCESS, v_state);
    }


    const my_struct &expected_struct = my_struct{7};
    float expected_float = 1.0f;
    double expected_double = 3.0;
    std::vector<float> expected_vector{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    std::vector<float> to_be_moved = expected_vector;

    const custom_object result = custom_object{expected_struct, expected_float, expected_double, to_be_moved};

    gempba::task_packet serialized_result = v_result_serializer(result);
    v_node.set_result(serialized_result);

    std::any any_result = v_node.get_any_result();
    auto object = std::any_cast<custom_object>(any_result);

    gempba::node_state state = v_node.get_state();
    ASSERT_EQ(gempba::node_state::RETRIEVED, state);
    ASSERT_EQ(expected_struct, object.m_my_struct);
    ASSERT_FLOAT_EQ(expected_float, object.m_f_value);
    ASSERT_DOUBLE_EQ(expected_double, object.m_d_value);
    ASSERT_EQ(expected_vector, object.m_vector);
}


// From this point onwards

TEST_F(node_core_impl_test, dummy_node) {
    gempba::node v_dummy_node = gempba::node_factory::create_dummy_node(m_balancer_mock);
    /// TODO ...
}

TEST_F(node_core_impl_test, get_pointers_non_dummy_node) {
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int p_val, gempba::node) {
    };
    int v_dummy_value = 7;

    gempba::node v_empty_node = gempba::node();
    gempba::node v_node = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_empty_node, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_FALSE(v_node.is_dummy());
    ASSERT_TRUE(v_node.get_parent() == nullptr);
    ASSERT_TRUE(v_node.get_root() != nullptr);
    ASSERT_EQ(0, v_node.get_children_count());
}

TEST_F(node_core_impl_test, three_level_nodes_with_dummy_node) {
    auto v_parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */
    std::function<void(std::thread::id, int, gempba::node)> v_dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int v_dummy_value = 7;

    auto n11 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto n21 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n11, v_dummy_function, std::make_tuple(v_dummy_value));
    auto n22 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n11, v_dummy_function, std::make_tuple(v_dummy_value));

    auto n12 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_parent, v_dummy_function, std::make_tuple(v_dummy_value));
    auto n23 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n12, v_dummy_function, std::make_tuple(v_dummy_value));
    auto n24 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n12, v_dummy_function, std::make_tuple(v_dummy_value));

    ASSERT_EQ(2, v_parent.get_children_count());
    ASSERT_EQ(v_parent, n11.get_parent());
    ASSERT_EQ(v_parent, n12.get_parent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11.get_children_count());
    ASSERT_EQ(n11, n21.get_parent());
    ASSERT_EQ(n11, n22.get_parent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12.get_children_count());
    ASSERT_EQ(n12, n23.get_parent());
    ASSERT_EQ(n12, n24.get_parent());

    // all nodes should have the same root
    ASSERT_EQ(n11.get_root(), v_parent);
    ASSERT_EQ(n21.get_root(), v_parent);
    ASSERT_EQ(n22.get_root(), v_parent);
    ASSERT_EQ(n12.get_root(), v_parent);
    ASSERT_EQ(n23.get_root(), v_parent);
    ASSERT_EQ(n24.get_root(), v_parent);
}

TEST_F(node_core_impl_test, get_root_test) {
    auto parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    try {
        parent.set_parent(parent);
        FAIL();
    } catch ([[maybe_unused]] std::exception &e) {
    };

    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    /**
     * The following lines should not be instantiated in practice, yet this is only for testing purposes
     * every node is spawned on a different thread because there is only one root per thread
     * this is done with the sole purpose of testing the parent/child association
     */
    std::vector<gempba::node> nodes;
    // spawn threads
    std::mutex vec_mutex;
    std::vector<std::thread> threads;
    for (int i = 0; i < 6; ++i) {
        threads.emplace_back([&] {
            std::scoped_lock lock(vec_mutex);

            auto v_dummy = gempba::node();
            nodes.emplace_back(gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value)));
        });
    }
    for (auto &thread: threads) {
        thread.join();
    }

    auto n11 = nodes[0];
    auto n21 = nodes[1];
    auto n22 = nodes[2];

    auto n12 = nodes[3];
    auto n23 = nodes[4];
    auto n24 = nodes[5];

    // Every node should be its own root
    ASSERT_EQ(n11.get_root(), n11);
    ASSERT_EQ(n21.get_root(), n21);
    ASSERT_EQ(n22.get_root(), n22);
    ASSERT_EQ(n12.get_root(), n12);
    ASSERT_EQ(n23.get_root(), n23);
    ASSERT_EQ(n24.get_root(), n24);
}

TEST_F(node_core_impl_test, add_child_and_set_parent_test) {
    auto parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    /**
     *              parent
     *              /    \
     *             n11    n12
     *           /  \     /  \
     *          n21  n22  n23  n24
     */

    try {
        parent.set_parent(parent);
        FAIL();
    } catch ([[maybe_unused]] std::exception &e) {
    };

    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto n11 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto n21 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n11, dummy_function, std::make_tuple(dummy_value));
    auto n22 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n11, dummy_function, std::make_tuple(dummy_value));

    auto n12 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto n23 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n12, dummy_function, std::make_tuple(dummy_value));
    auto n24 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, n12, dummy_function, std::make_tuple(dummy_value));

    // set_parent and add_child are called when instantiating as the parent of a node is required

    ASSERT_EQ(2, parent.get_children_count());
    ASSERT_EQ(parent, n11.get_parent());
    ASSERT_EQ(parent, n12.get_parent());

    // n11 is the parent of n21 and n22
    ASSERT_EQ(2, n11.get_children_count());
    ASSERT_EQ(n11, n21.get_parent());
    ASSERT_EQ(n11, n22.get_parent());

    // n12 is the parent of n23 and n24
    ASSERT_EQ(2, n12.get_children_count());
    ASSERT_EQ(n12, n23.get_parent());
    ASSERT_EQ(n12, n24.get_parent());

    // all nodes should have the same root
    ASSERT_EQ(n11.get_root(), parent);
    ASSERT_EQ(n21.get_root(), parent);
    ASSERT_EQ(n22.get_root(), parent);
    ASSERT_EQ(n12.get_root(), parent);
    ASSERT_EQ(n23.get_root(), parent);
    ASSERT_EQ(n24.get_root(), parent);
}

TEST_F(node_core_impl_test, set_get_states) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto v_dummy = gempba::node();
    auto v_node = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value));

    ASSERT_EQ(gempba::UNUSED, v_node.get_state());
    ASSERT_FALSE(v_node.is_consumed());

    v_node.set_state(gempba::PUSHED);
    ASSERT_EQ(gempba::PUSHED, v_node.get_state());
    ASSERT_TRUE(v_node.is_consumed());

    v_node.set_state(gempba::FORWARDED);
    ASSERT_EQ(gempba::FORWARDED, v_node.get_state());
    ASSERT_TRUE(v_node.is_consumed());

    v_node.set_state(gempba::DISCARDED);
    ASSERT_EQ(gempba::DISCARDED, v_node.get_state());
    ASSERT_TRUE(v_node.is_consumed());
}

TEST_F(node_core_impl_test, is_result_retrievable) {
    std::function<int(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) { return 5; };
    int dummy_value = 7;

    auto v_dummy = gempba::node();
    auto v_node = gempba::node_factory::create_explicit_node<int>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value));

    v_node.run();
    ASSERT_TRUE(v_node.is_result_ready());
    auto actual_any = v_node.get_any_result();
    ASSERT_TRUE(actual_any.has_value());

    const auto actual = std::any_cast<int>(actual_any);
    ASSERT_EQ(5, actual);

    ASSERT_FALSE(v_node.is_result_ready());
}

TEST_F(node_core_impl_test, forward_push_count_and_thread_id) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    std::thread::id expected_thread_id = std::this_thread::get_id();
    auto v_dummy = gempba::node();
    auto v_node = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value));

    ASSERT_EQ(0, v_node.get_forward_count());
    ASSERT_EQ(0, v_node.get_push_count());

    v_node.set_state(gempba::FORWARDED);
    ASSERT_EQ(1, v_node.get_forward_count());

    v_node.set_state(gempba::PUSHED);
    ASSERT_EQ(1, v_node.get_push_count());

    ASSERT_EQ(expected_thread_id, v_node.get_thread_id());
}

TEST_F(node_core_impl_test, get_node_id) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto v_dummy = gempba::node();
    auto node1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value));
    auto node2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, node1, dummy_function, std::make_tuple(dummy_value));

    ASSERT_EQ(1, node1.get_node_id());
    ASSERT_EQ(2, node2.get_node_id());
}

TEST_F(node_core_impl_test, serialize_deserialize) {
    custom_object v_expected_after_deserialized;
    std::function<void(std::thread::id, custom_object, gempba::node)> v_dummy_function =
            [&v_expected_after_deserialized](std::thread::id, custom_object object, gempba::node p_node) { v_expected_after_deserialized = std::move(object); };
    const my_struct ins{7};
    constexpr float f1 = 1.0f;
    constexpr double d1 = 3.0;
    const std::vector vec{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
    auto v_temp = vec;

    custom_object v_object(ins, f1, d1, v_temp); // argument to be sent remotely

    std::function<gempba::task_packet(custom_object)> v_args_serializer = [](custom_object p_object) {
        std::stringstream v_ss;
        boost::archive::text_oarchive v_archive(v_ss);
        v_archive << p_object;
        return gempba::task_packet(v_ss.str());
    };
    std::function<std::tuple<custom_object>(gempba::task_packet)> v_args_deserializer = [](gempba::task_packet p_task) {
        custom_object v_object;

        std::stringstream v_ss;
        v_ss.write(reinterpret_cast<const char *>(p_task.data()), static_cast<int>(p_task.size()));
        boost::archive::text_iarchive v_archive(v_ss);
        v_archive >> v_object;

        return std::make_tuple(v_object);
    };


    // node that holds the argument
    auto v_node = gempba::node_factory::create_serializable_explicit_node<void>(m_balancer_mock, gempba::node(), v_dummy_function, std::make_tuple(v_object), v_args_serializer,
                                                                                v_args_deserializer);
    // this simulates serializing the arguments that will be sent remotely
    const gempba::task_packet args_serialized = v_node.serialize();
    const std::string args_serialized_str(reinterpret_cast<const char *>(args_serialized.data()), args_serialized.size());

    const std::string expected_args_serialized = "22 serialization::archive 20 0 0 0 0 7 1.000000000e+00 3.00000000000000000e+00 10 0 1.000000000e+00 2.000000000e+00 "
            "3.000000000e+00 4.000000000e+00 5.000000000e+00 6.000000000e+00 7.000000000e+00 8.000000000e+00 9.000000000e+00 1.000000000e+01";
    // assert that the copy is equal to the reference
    ASSERT_EQ(gempba::test_utils::strip_boost_metadata(expected_args_serialized), gempba::test_utils::strip_boost_metadata(args_serialized_str));

    // Remote node that will be populated with the serialized arguments
    auto v_deserialized_node = gempba::node_factory::create_serializable_node<void>(m_balancer_mock, gempba::node(), v_dummy_function, v_args_serializer, v_args_deserializer);
    v_deserialized_node.deserialize(args_serialized);
    v_deserialized_node.run(); // run the deserialized node

    // assert the expected object is equal to the deserialized object
    ASSERT_EQ(v_expected_after_deserialized, v_object);
}


TEST_F(node_core_impl_test, get_children) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto dummy_parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    auto child1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child3 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));

    auto children = dummy_parent.get_children();
    ASSERT_EQ(3, children.size());
    ASSERT_EQ(child1, dummy_parent.get_leftmost_child());

    auto it = children.begin();
    ASSERT_EQ(child1, *(it++));
    ASSERT_EQ(child2, *(it++));
    ASSERT_EQ(child3, *it);
}

TEST_F(node_core_impl_test, remove_leftmost_child) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto dummy_parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    auto child1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child3 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));

    // assert existing children
    auto children = dummy_parent.get_children();

    auto it = children.begin();
    ASSERT_EQ(child1, *it++);
    ASSERT_EQ(child2, *it++);
    ASSERT_EQ(child3, *it);

    // prune first child
    dummy_parent.remove_leftmost_child();
    children = dummy_parent.get_children();
    it = children.begin();
    ASSERT_EQ(2, children.size());
    ASSERT_EQ(child2, *it++);
    ASSERT_EQ(child3, *it);

    // prune first child again
    dummy_parent.remove_leftmost_child();
    children = dummy_parent.get_children();
    it = children.begin();
    ASSERT_EQ(child3, *it);
    ASSERT_EQ(1, children.size());

    // prune first child again
    dummy_parent.remove_leftmost_child();
    children = dummy_parent.get_children();
    ASSERT_EQ(0, children.size());
}

TEST_F(node_core_impl_test, remove_second_leftmost_child) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto dummy_parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    auto child1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child3 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));

    // assert existing children
    auto children = dummy_parent.get_children();

    auto it = children.begin();
    ASSERT_EQ(child1, *it++);
    ASSERT_EQ(child2, *it++);
    ASSERT_EQ(child3, *it);

    // prune second child

    dummy_parent.remove_second_leftmost_child();
    children = dummy_parent.get_children();
    it = children.begin();
    ASSERT_EQ(child1, *it++);
    ASSERT_EQ(child3, *it);
    ASSERT_EQ(2, children.size());

    // prune second child again
    dummy_parent.remove_second_leftmost_child();
    children = dummy_parent.get_children();
    it = children.begin();
    ASSERT_EQ(child1, *it);
    ASSERT_EQ(1, children.size());

    // prune second child again
    try {
        dummy_parent.remove_second_leftmost_child();
        FAIL() << "Expected exception";
    } catch (const std::exception &e) {
        ASSERT_STREQ("Cannot prune second child when there are less than 2 children", e.what());
    }
}

TEST_F(node_core_impl_test, get_second_leftmost_child) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto dummy_parent = gempba::node_factory::create_dummy_node(m_balancer_mock);
    auto child1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));
    auto child3 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, dummy_parent, dummy_function, std::make_tuple(dummy_value));

    // assert existing children
    auto children = dummy_parent.get_children();

    auto it = children.begin();
    ASSERT_EQ(child1, *it++);
    ASSERT_EQ(child2, *it++);
    ASSERT_EQ(child3, *it);

    // get second child
    ASSERT_EQ(child2, dummy_parent.get_second_leftmost_child());
}

TEST_F(node_core_impl_test, get_siblings) {
    std::function<void(std::thread::id, int, gempba::node)> dummy_function = [](std::thread::id, int p_val, gempba::node p_node) {
    };
    int dummy_value = 7;

    auto dummy = gempba::node_factory::create_dummy_node(m_balancer_mock);

    ASSERT_EQ(nullptr, dummy.get_leftmost_sibling());
    ASSERT_EQ(nullptr, dummy.get_left_sibling());
    ASSERT_EQ(nullptr, dummy.get_right_sibling());

    auto v_dummy = gempba::node();
    auto parent = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, v_dummy, dummy_function, std::make_tuple(dummy_value));
    ASSERT_EQ(nullptr, parent.get_leftmost_sibling());
    ASSERT_EQ(nullptr, parent.get_left_sibling());
    ASSERT_EQ(nullptr, parent.get_right_sibling());

    auto child1 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto child2 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto child3 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto child4 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));
    auto child5 = gempba::node_factory::create_explicit_node<void>(m_balancer_mock, parent, dummy_function, std::make_tuple(dummy_value));

    ASSERT_EQ(child1, child1.get_leftmost_sibling());
    ASSERT_EQ(nullptr, child1.get_left_sibling());
    ASSERT_EQ(child2, child1.get_right_sibling());

    ASSERT_EQ(child1, child2.get_leftmost_sibling());
    ASSERT_EQ(child1, child2.get_left_sibling());
    ASSERT_EQ(child3, child2.get_right_sibling());

    ASSERT_EQ(child1, child3.get_leftmost_sibling());
    ASSERT_EQ(child2, child3.get_left_sibling());
    ASSERT_EQ(child4, child3.get_right_sibling());

    ASSERT_EQ(child1, child4.get_leftmost_sibling());
    ASSERT_EQ(child3, child4.get_left_sibling());
    ASSERT_EQ(child5, child4.get_right_sibling());

    ASSERT_EQ(child1, child5.get_leftmost_sibling());
    ASSERT_EQ(child4, child5.get_left_sibling());
    ASSERT_EQ(nullptr, child5.get_right_sibling());
}
