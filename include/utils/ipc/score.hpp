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
#ifndef SCORE_HPP
#define SCORE_HPP

#include <array>
#include <bit>
#include <compare>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

// Compatible with C++20 and laters

namespace gempba {

    enum class score_type : std::uint8_t { I32, I64, F32, F64, F128 };

    /**
     * @brief Represents a fixed-size, type-safe numeric value container.
     *
     * Holds one of the supported numeric types (int32_t, int64_t, float, double, long double)
     * in a compact structure for efficient storage and safe access.
     * Designed for use cases like serialization or inter-process communication.
     */
    class score {
    public:
        static constexpr std::size_t PAYLOAD_SIZE = 16; // must hold long double
    private:
        score_type m_kind;
        std::array<std::byte, PAYLOAD_SIZE> m_payload;

    public:
        /**
         * Factory for supported numeric types
         * @tparam T supported numeric type
         * @param p_value value to be stored in the score
         * @return score instance containing the value
         */
        template<typename T>
        static constexpr score make(T p_value) noexcept {
            static_assert(IS_SUPPORTED<T>, "unsupported score type");
            static_assert(sizeof(T) <= PAYLOAD_SIZE, "type too large for score payload");
            score v_score{};
            v_score.m_kind = TYPE_OF<T>;
            auto v_bytes = std::bit_cast<std::array<std::byte, sizeof(T)> >(p_value);
            std::memcpy(v_score.m_payload.data(), v_bytes.data(), sizeof(T));
            // zero remaining payload bytes when sizeof(T) < payload_size
            if constexpr (sizeof(T) < PAYLOAD_SIZE) {
                std::memset(v_score.m_payload.data() + sizeof(T), 0, PAYLOAD_SIZE - sizeof(T));
            }
            return v_score;
        }

        // Strong ordering
        friend constexpr auto operator<=>(const score &p_lhs, const score &p_rhs) noexcept {
            return to_long_double(p_lhs) <=> to_long_double(p_rhs);
        }

        friend constexpr bool operator==(const score &p_lhs, const score &p_rhs) noexcept {
            return to_long_double(p_lhs) == to_long_double(p_rhs);
        }

        /**
         * Retrieves the stored value as a specific type.
         * @tparam T type to retrieve the value as
         * @return value of type T stored in the score
         */
        template<typename T>
        [[nodiscard]] T get() const {
            static_assert(IS_SUPPORTED<T>, "unsupported score type");
            static_assert(sizeof(T) <= PAYLOAD_SIZE, "requested type too large for score payload");
            if (m_kind != TYPE_OF<T>) {
                throw std::runtime_error{"score: wrong target type"};
            }
            T v_out{};
            std::memcpy(&v_out, m_payload.data(), sizeof(T));
            return v_out;
        }

        /**
         * Attempts to retrieve the stored value as a specific type.
         * @tparam T type to retrieve the value as
         * @param p_out reference to output variable where the value will be stored if successful
         * @return true if the type matches and the value was copied, false otherwise
         */
        template<typename T>
        [[nodiscard]] bool try_get(T &p_out) const noexcept {
            static_assert(IS_SUPPORTED<T>, "unsupported score type");
            static_assert(sizeof(T) <= PAYLOAD_SIZE, "requested type too large for score payload");
            if (m_kind != TYPE_OF<T>) {
                return false;
            }
            std::memcpy(&p_out, m_payload.data(), sizeof(T));
            return true;
        }

        /**
         * Retrieves the stored value as a specific type, allowing for conversion.
         * @tparam T type to retrieve the value as
         * @return value of type T converted from the stored value
         */
        template<typename T>
        [[nodiscard]] T get_loose() const noexcept {
            switch (m_kind) {
                case score_type::I32: {
                    int32_t v_value{};
                    std::memcpy(&v_value, m_payload.data(), sizeof(v_value));
                    if constexpr (std::is_integral_v<T>) {
                        return static_cast<T>(v_value);
                    } else {
                        return static_cast<T>(static_cast<long double>(v_value));
                    }
                }
                case score_type::I64: {
                    int64_t v_value{};
                    std::memcpy(&v_value, m_payload.data(), sizeof(v_value));
                    if constexpr (std::is_integral_v<T>) {
                        return static_cast<T>(v_value);
                    } else {
                        return static_cast<T>(static_cast<long double>(v_value));
                    }
                }
                case score_type::F32: {
                    float v_value{};
                    std::memcpy(&v_value, m_payload.data(), sizeof(v_value));
                    return static_cast<T>(v_value);
                }
                case score_type::F64: {
                    double v_value{};
                    std::memcpy(&v_value, m_payload.data(), sizeof(v_value));
                    return static_cast<T>(v_value);
                }
                case score_type::F128: {
                    long double v_value{};
                    std::memcpy(&v_value, m_payload.data(), sizeof(v_value));
                    return static_cast<T>(v_value);
                }
            }
            std::abort(); // std::unreachable in C++23
        }

        [[nodiscard]] std::string to_string() const noexcept {
            std::ostringstream oss;
            switch (m_kind) {
                case score_type::I32: {
                    oss << get_loose<int32_t>();
                    break;
                }
                case score_type::I64: {
                    oss << get_loose<int64_t>();
                    break;
                }
                case score_type::F32: {
                    oss << std::setprecision(std::numeric_limits<float>::max_digits10) << get_loose<float>();
                    break;
                }
                case score_type::F64: {
                    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << get_loose<double>();
                    break;
                }
                case score_type::F128:
                    oss << std::setprecision(std::numeric_limits<long double>::max_digits10) << get_loose<long double>();
                    break;

            }
            return oss.str();
        }

    private:
        template<typename T>
        static constexpr bool IS_SUPPORTED =
                std::same_as<std::remove_cvref_t<T>, std::int32_t> ||
                std::same_as<std::remove_cvref_t<T>, std::int64_t> ||
                std::same_as<std::remove_cvref_t<T>, float> ||
                std::same_as<std::remove_cvref_t<T>, double> ||
                std::same_as<std::remove_cvref_t<T>, long double>;

        template<typename T>
        static constexpr score_type TYPE_OF =
                std::same_as<std::remove_cvref_t<T>, std::int32_t>
                    ? score_type::I32
                    : std::same_as<std::remove_cvref_t<T>, std::int64_t>
                    ? score_type::I64
                    : std::same_as<std::remove_cvref_t<T>, float>
                    ? score_type::F32
                    : std::same_as<std::remove_cvref_t<T>, double>
                    ? score_type::F64
                    : score_type::F128;

        static constexpr long double to_long_double(const score &p_score) noexcept {
            switch (p_score.m_kind) {
                case score_type::I32: {
                    std::int32_t v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::I64: {
                    std::int64_t v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::F32: {
                    float v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::F64: {
                    double v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::F128: {
                    long double v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return v_value;
                }
            }
            std::abort(); // std::unreachable in C++23
        }
    };

    /*--------------------------------------
     * layout checks
     *-------------------------------------*/
    static_assert(sizeof(score) == (1 + score::PAYLOAD_SIZE), "score must be exactly 1 + payload_size bytes");
    static_assert(std::is_trivially_copyable_v<score>, "score must be trivially copyable");
    static_assert(std::is_standard_layout_v<score>, "score must be standard layout");

}

#endif // SCORE_HPP
