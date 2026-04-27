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
#include <cmath>
#include <gtest/gtest.h>
#include <iomanip>
#include <limits>
#include <sstream>

#include <gempba/utils/score.hpp>

using namespace gempba;

namespace {
    template<typename T>
    T max_minus_one() {
        if constexpr (std::is_integral_v<T>)
            return std::numeric_limits<T>::max() - 1;
        else
            return std::nextafter(std::numeric_limits<T>::max(), std::numeric_limits<T>::lowest());
    }

    template<typename T>
    T max_value() {
        return std::numeric_limits<T>::max();
    }

} // namespace


TEST(score_test, equality_and_ordering_test) {
    auto v_score_i32_a = score::make(max_minus_one<int32_t>());
    auto v_score_i32_b = score::make(max_value<int32_t>());

    auto v_score_i64_a = score::make(max_minus_one<int64_t>());
    auto v_score_i64_b = score::make(max_value<int64_t>());

    auto v_score_f32_a = score::make(max_minus_one<float>());
    auto v_score_f32_b = score::make(max_value<float>());

    auto v_score_f64_a = score::make(max_minus_one<double>());
    auto v_score_f64_b = score::make(max_value<double>());

    auto v_score_f128_a = score::make(max_minus_one<long double>());
    auto v_score_f128_b = score::make(max_value<long double>());

    EXPECT_NE(v_score_i32_a, v_score_i32_b);
    EXPECT_NE(v_score_i64_a, v_score_i64_b);
    EXPECT_NE(v_score_f32_a, v_score_f32_b);
    EXPECT_NE(v_score_f64_a, v_score_f64_b);
    EXPECT_NE(v_score_f128_a, v_score_f128_b);

    EXPECT_LT(v_score_i32_a, v_score_i32_b);
    EXPECT_LT(v_score_f32_a, v_score_f32_b);
    EXPECT_LT(v_score_f128_a, v_score_f128_b);
    EXPECT_GT(v_score_i64_b, v_score_i64_a);
    EXPECT_GT(v_score_f64_b, v_score_f64_a);
    EXPECT_GT(v_score_f128_b, v_score_f128_a);
}

TEST(score_test, get_correct_and_wrong_type_test) {
    auto v_score_i32 = score::make<int32_t>(42);
    auto v_score_i64 = score::make<int64_t>(100);
    auto v_score_f32 = score::make<float>(3.14f);
    auto v_score_f64 = score::make<double>(2.718);
    auto v_score_f128 = score::make<long double>(1.2345L);

    EXPECT_EQ(v_score_i32.get<int32_t>(), 42);
    EXPECT_EQ(v_score_i64.get<int64_t>(), 100);
    EXPECT_FLOAT_EQ(v_score_f32.get<float>(), 3.14f);
    EXPECT_DOUBLE_EQ(v_score_f64.get<double>(), 2.718);
    EXPECT_EQ(v_score_f128.get<long double>(), 1.2345L);

    EXPECT_THROW(v_score_i32.get<int64_t>(), std::runtime_error);
    EXPECT_THROW(v_score_i32.get<float>(), std::runtime_error);
    EXPECT_THROW(v_score_f32.get<double>(), std::runtime_error);
    EXPECT_THROW(v_score_f64.get<int32_t>(), std::runtime_error);
    EXPECT_THROW(v_score_f128.get<int32_t>(), std::runtime_error);
    EXPECT_THROW(v_score_f128.get<double>(), std::runtime_error);
}

TEST(score_test, try_get_success_and_failure_test) {
    auto v_score = score::make<int64_t>(999);
    int64_t v_correct{};
    int32_t v_wrong{};
    long double v_wrong_f128{};
    EXPECT_TRUE(v_score.try_get(v_correct));
    EXPECT_FALSE(v_score.try_get(v_wrong));
    EXPECT_FALSE(v_score.try_get(v_wrong_f128));
    EXPECT_EQ(v_correct, 999);
}

TEST(score_test, exhaustive_get_loose_matrix_test) {
    int32_t v_vi32 = -42;
    int64_t v_vi64 = 1234567890123LL;
    float v_vf32 = 1.5f;
    double v_vf64 = -3.25;
    long double v_vf128 = 9.87654321L;

    auto v_score_i32 = score::make(v_vi32);
    auto v_score_i64 = score::make(v_vi64);
    auto v_score_f32 = score::make(v_vf32);
    auto v_score_f64 = score::make(v_vf64);
    auto v_score_f128 = score::make(v_vf128);

    // From i32
    EXPECT_EQ(v_score_i32.get_loose<int32_t>(), v_vi32);
    EXPECT_EQ(v_score_i32.get_loose<int64_t>(), v_vi32);
    EXPECT_FLOAT_EQ(v_score_i32.get_loose<float>(), static_cast<float>(v_vi32));
    EXPECT_DOUBLE_EQ(v_score_i32.get_loose<double>(), static_cast<double>(v_vi32));
    EXPECT_EQ(v_score_i32.get_loose<long double>(), static_cast<long double>(v_vi32));

    // From i64
    EXPECT_EQ(v_score_i64.get_loose<int32_t>(), static_cast<int32_t>(v_vi64));
    EXPECT_EQ(v_score_i64.get_loose<int64_t>(), v_vi64);
    EXPECT_FLOAT_EQ(v_score_i64.get_loose<float>(), static_cast<float>(v_vi64));
    EXPECT_DOUBLE_EQ(v_score_i64.get_loose<double>(), static_cast<double>(v_vi64));
    EXPECT_EQ(v_score_i64.get_loose<long double>(), static_cast<long double>(v_vi64));

    // From f32
    EXPECT_EQ(v_score_f32.get_loose<int32_t>(), static_cast<int32_t>(v_vf32));
    EXPECT_EQ(v_score_f32.get_loose<int64_t>(), static_cast<int64_t>(v_vf32));
    EXPECT_FLOAT_EQ(v_score_f32.get_loose<float>(), v_vf32);
    EXPECT_DOUBLE_EQ(v_score_f32.get_loose<double>(), static_cast<double>(v_vf32));
    EXPECT_EQ(v_score_f32.get_loose<long double>(), static_cast<long double>(v_vf32));

    // From f64
    EXPECT_EQ(v_score_f64.get_loose<int32_t>(), static_cast<int32_t>(v_vf64));
    EXPECT_EQ(v_score_f64.get_loose<int64_t>(), static_cast<int64_t>(v_vf64));
    EXPECT_FLOAT_EQ(v_score_f64.get_loose<float>(), static_cast<float>(v_vf64));
    EXPECT_DOUBLE_EQ(v_score_f64.get_loose<double>(), v_vf64);
    EXPECT_EQ(v_score_f64.get_loose<long double>(), static_cast<long double>(v_vf64));

    // From f128
    EXPECT_EQ(v_score_f128.get_loose<int32_t>(), static_cast<int32_t>(v_vf128));
    EXPECT_EQ(v_score_f128.get_loose<int64_t>(), static_cast<int64_t>(v_vf128));
    EXPECT_FLOAT_EQ(v_score_f128.get_loose<float>(), static_cast<float>(v_vf128));
    EXPECT_DOUBLE_EQ(v_score_f128.get_loose<double>(), static_cast<double>(v_vf128));
    EXPECT_EQ(v_score_f128.get_loose<long double>(), v_vf128);
}

TEST(score_test, performance_benchmark_test) {
    constexpr std::size_t v_number_of_samples = 500'000;
    std::vector<score> v_scores;
    v_scores.reserve(v_number_of_samples);

    const auto v_start_make = std::chrono::high_resolution_clock::now();
    for (std::size_t i = 0; i < v_number_of_samples; ++i) {
        v_scores.push_back(score::make<int32_t>(static_cast<int32_t>(i)));
    }
    const auto v_end_make = std::chrono::high_resolution_clock::now();

    volatile int64_t v_sum = 0;
    const auto v_start_get = std::chrono::high_resolution_clock::now();
    for (const auto &v_score: v_scores) {
        v_sum += v_score.get_loose<int64_t>();
    }
    const auto v_end_get = std::chrono::high_resolution_clock::now();

    const auto v_make_ms = std::chrono::duration_cast<std::chrono::milliseconds>(v_end_make - v_start_make).count();
    const auto v_get_ms = std::chrono::duration_cast<std::chrono::milliseconds>(v_end_get - v_start_get).count();

    std::cout << "[ PERF ] Created " << v_number_of_samples << " scores in " << v_make_ms << " ms\n";
    std::cout << "[ PERF ] Retrieved & summed in " << v_get_ms << " ms\n";
    std::cout << "[ PERF ] Sum(ignore): " << v_sum << "\n";
}


TEST(score_test, to_string) {
    // Integer tests
    const auto v_score_i32 = score::make<int32_t>(42);
    EXPECT_EQ(v_score_i32.to_string(), "42");

    const auto v_score_i64 = score::make<int64_t>(9223372036854775807LL);
    EXPECT_EQ(v_score_i64.to_string(), "9223372036854775807");

    // Floating-point tests
    constexpr float v_pi_float = 3.14159274f; // max precision float
    const auto v_score_f32 = score::make(v_pi_float);
    const std::string v_expected_f32 = "3.14159274";
    const std::string v_actual_f32 = v_score_f32.to_string();
    EXPECT_EQ(v_expected_f32, v_actual_f32);

    constexpr double v_pi_d = 3.1415926535897931;
    const auto v_score_f64 = score::make(v_pi_d);
    const std::string v_expected_f64 = "3.1415926535897931";
    const std::string v_actual_f64 = v_score_f64.to_string();
    EXPECT_EQ(v_expected_f64, v_actual_f64);

    // Long double / F128 test
    constexpr long double v_pi_ld = 3.14159265358979323851L;
    const auto v_score_f128 = score::make(v_pi_ld);
    std::ostringstream v_oss_f128;
    v_oss_f128 << std::setprecision(std::numeric_limits<long double>::max_digits10) << v_pi_ld;
    const std::string v_expected_f128 = v_oss_f128.str();
    const std::string v_actual_f128 = v_score_f128.to_string();
    EXPECT_EQ(v_expected_f128, v_actual_f128);
}


TEST(score_test, make_and_get_u_i32) {
    constexpr auto v_value = std::uint32_t{1'234'567'890};
    const auto v_score = score::make(v_value);

    // direct get
    EXPECT_EQ(v_score.get<std::uint32_t>(), 1'234'567'890);

    // try_get success
    std::uint32_t v_ok{};
    EXPECT_TRUE(v_score.try_get(v_ok));
    EXPECT_EQ(v_ok, 1234567890);

    // try_get wrong type
    std::int64_t v_wrong{};
    EXPECT_FALSE(v_score.try_get(v_wrong));

    // to_string
    EXPECT_EQ(v_score.to_string(), "1234567890");
}

TEST(score_test, make_and_get_u_i64) {
    constexpr auto v_value = std::uint64_t{12'345'678'901'234'567'890ULL};
    const auto v_score = score::make(v_value);

    // direct get
    EXPECT_EQ(v_score.get<std::uint64_t>(), 12'345'678'901'234'567'890ULL);

    // try_get success
    std::uint64_t v_ok{};
    EXPECT_TRUE(v_score.try_get(v_ok));
    EXPECT_EQ(v_ok, 12'345'678'901'234'567'890ULL);

    // try_get wrong type
    std::int64_t v_wrong{};
    EXPECT_FALSE(v_score.try_get(v_wrong));

    // to_string
    EXPECT_EQ(v_score.to_string(), "12345678901234567890");
}

// copy/paste of make_and_get_u_i64, just to be explicit
TEST(score_test, make_and_get_size_t) {
    constexpr auto v_value = std::size_t{12'345'678'901'234'567'890ULL};
    const auto v_score = score::make(v_value);

    // direct get
    EXPECT_EQ(v_score.get<std::size_t>(), 12'345'678'901'234'567'890ULL);

    // try_get success
    std::size_t v_ok{};
    EXPECT_TRUE(v_score.try_get(v_ok));
    EXPECT_EQ(v_ok, 12'345'678'901'234'567'890ULL);

    // try_get wrong type
    std::int64_t v_wrong{};
    EXPECT_FALSE(v_score.try_get(v_wrong));

    // to_string
    EXPECT_EQ(v_score.to_string(), "12345678901234567890");
}

TEST(score_test, compare_u_i32_values) {
    const auto v_a = score::make(std::uint32_t{42});
    const auto v_b = score::make(std::uint32_t{99});

    EXPECT_LT(v_a, v_b);
    EXPECT_GT(v_b, v_a);
}

TEST(score_test, compare_u_i64_values) {
    const auto v_a = score::make(std::uint64_t{42});
    const auto v_b = score::make(std::uint64_t{99});

    EXPECT_LT(v_a, v_b);
    EXPECT_GT(v_b, v_a);
}

TEST(score_test, cross_type_not_equal_u_i32) {
    const auto v_ui32 = score::make(std::uint32_t{100});
    const auto v_i32 = score::make(std::int32_t{100});

    EXPECT_NE(v_ui32, v_i32);
}

TEST(score_test, cross_type_not_equal_u_i64) {
    const auto v_ui32 = score::make(std::uint64_t{100});
    const auto v_i32 = score::make(std::int64_t{100});

    EXPECT_NE(v_ui32, v_i32);
}

TEST(score_test, cross_type_not_equal_u_i32_and_u_i64) {
    const auto v_ui32 = score::make(std::uint32_t{100});
    const auto v_ui64 = score::make(std::uint64_t{100});

    EXPECT_NE(v_ui32, v_ui64);
}

TEST(score_test, kind_returns_score_type_tag) {
    EXPECT_EQ(score_type::I32, score::make(std::int32_t{1}).kind());
    EXPECT_EQ(score_type::U_I32, score::make(std::uint32_t{1}).kind());
    EXPECT_EQ(score_type::I64, score::make(std::int64_t{1}).kind());
    EXPECT_EQ(score_type::U_I64, score::make(std::uint64_t{1}).kind());
    EXPECT_EQ(score_type::F32, score::make(1.0f).kind());
    EXPECT_EQ(score_type::F64, score::make(1.0).kind());
    EXPECT_EQ(score_type::F128, score::make(1.0L).kind());
}

TEST(score_test, to_raw_round_trip_for_each_supported_type) {
    const auto v_round_trip = []<typename T>(T p_value) {
        const auto v_original = score::make(p_value);
        const auto v_reconstructed = score::from_raw(v_original.kind(), v_original.to_raw());
        EXPECT_EQ(v_original, v_reconstructed);
        EXPECT_EQ(p_value, v_reconstructed.template get<T>());
    };
    v_round_trip(std::int32_t{-12345});
    v_round_trip(std::uint32_t{0xDEADBEEF});
    v_round_trip(std::int64_t{-1234567890123LL});
    v_round_trip(std::uint64_t{0xCAFEBABEDEADBEEFULL});
    v_round_trip(3.1415927f);
    v_round_trip(2.718281828459045);
}

TEST(score_test, to_raw_aborts_for_f128) {
    const auto v_score = score::make(1.0L);
    EXPECT_DEATH(static_cast<void>(v_score.to_raw()), "");
}

TEST(score_test, from_raw_aborts_for_f128) { EXPECT_DEATH(static_cast<void>(score::from_raw(score_type::F128, 0)), ""); }

TEST(score_test, equality_within_unsigned_types) {
    EXPECT_EQ(score::make(std::uint32_t{42}), score::make(std::uint32_t{42}));
    EXPECT_NE(score::make(std::uint32_t{42}), score::make(std::uint32_t{43}));
    EXPECT_EQ(score::make(std::uint64_t{42}), score::make(std::uint64_t{42}));
    EXPECT_NE(score::make(std::uint64_t{42}), score::make(std::uint64_t{43}));
}

TEST(score_test, ordering_across_different_kinds_uses_long_double) {
    // <=> falls back to long-double comparison when kinds differ. A monotonically
    // increasing chain that touches every score_type drives all branches of
    // to_long_double().
    const auto v_i32 = score::make(std::int32_t{1});
    const auto v_u32 = score::make(std::uint32_t{2});
    const auto v_i64 = score::make(std::int64_t{3});
    const auto v_u64 = score::make(std::uint64_t{4});
    const auto v_f32 = score::make(5.0f);
    const auto v_f64 = score::make(6.0);
    const auto v_f128 = score::make(7.0L);
    EXPECT_LT(v_i32, v_u32);
    EXPECT_LT(v_u32, v_i64);
    EXPECT_LT(v_i64, v_u64);
    EXPECT_LT(v_u64, v_f32);
    EXPECT_LT(v_f32, v_f64);
    EXPECT_LT(v_f64, v_f128);
    EXPECT_GT(v_f128, v_i32);
}

TEST(score_test, get_loose_converts_unsigned_integers_to_floating) {
    // Drives the U_I32 / U_I64 → floating branches of get_loose
    EXPECT_FLOAT_EQ(42.0f, score::make(std::uint32_t{42}).get_loose<float>());
    EXPECT_DOUBLE_EQ(42.0, score::make(std::uint32_t{42}).get_loose<double>());
    EXPECT_FLOAT_EQ(7.0f, score::make(std::uint64_t{7}).get_loose<float>());
    EXPECT_DOUBLE_EQ(7.0, score::make(std::uint64_t{7}).get_loose<double>());
}
