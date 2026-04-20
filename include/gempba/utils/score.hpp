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
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <type_traits>

// Compatible with C++23 and laters

namespace gempba {

    enum class score_type : std::uint8_t { I32, U_I32, I64, U_I64, F32, F64, F128 };

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


        /**
         * Helper factory for supported numeric types
         * @tparam T supported numeric type
         * @param p_value value to be stored in the score
         * @return score instance containing the value
         */
        template<typename T>
        static constexpr score make_raw(T p_value) noexcept {
            static_assert(IS_SUPPORTED<T>, "unsupported score type");
            static_assert(sizeof(T) <= PAYLOAD_SIZE, "type too large for score payload");
            score v_score{};
            v_score.m_kind = TYPE_OF<T>;
            auto v_bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(p_value);
            std::memcpy(v_score.m_payload.data(), v_bytes.data(), sizeof(T));
            if constexpr (sizeof(T) < PAYLOAD_SIZE) {
                std::memset(v_score.m_payload.data() + sizeof(T), 0, PAYLOAD_SIZE - sizeof(T));
            }
            return v_score;
        }

    public:
        /**
         * Factory for supported numeric types
         * @tparam T supported numeric type
         * @param p_value value to be stored in the score
         * @return score instance containing the value
         */
        template<typename T>
        static constexpr score make(T p_value) noexcept {
            using U = std::remove_cvref_t<T>;

            if constexpr (std::is_integral_v<U> && sizeof(U) == 4 && std::is_signed_v<U>) {
                return make_raw<std::int32_t>(static_cast<std::int32_t>(p_value));
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 4 && std::is_unsigned_v<U>) {
                return make_raw<std::uint32_t>(static_cast<std::uint32_t>(p_value));
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 8 && std::is_signed_v<U>) {
                return make_raw<std::int64_t>(static_cast<std::int64_t>(p_value));
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 8 && std::is_unsigned_v<U>) {
                return make_raw<std::uint64_t>(static_cast<std::uint64_t>(p_value));
            } else if constexpr (std::same_as<U, float> || std::same_as<U, double> || std::same_as<U, long double>) {
                return make_raw<U>(p_value);
            } else {
                static_assert(IS_SUPPORTED<U>, "unsupported score type");
            }
            std::unreachable();
        }

        friend constexpr std::partial_ordering operator<=>(const score &p_lhs, const score &p_rhs) noexcept {
            if (p_lhs.m_kind == p_rhs.m_kind) {
                switch (p_lhs.m_kind) {
                    case score_type::I32:
                        return read_as<std::int32_t>(p_lhs) <=> read_as<std::int32_t>(p_rhs);
                    case score_type::U_I32:
                        return read_as<std::uint32_t>(p_lhs) <=> read_as<std::uint32_t>(p_rhs);
                    case score_type::I64:
                        return read_as<std::int64_t>(p_lhs) <=> read_as<std::int64_t>(p_rhs);
                    case score_type::U_I64:
                        return read_as<std::uint64_t>(p_lhs) <=> read_as<std::uint64_t>(p_rhs);
                    case score_type::F32:
                        return read_as<float>(p_lhs) <=> read_as<float>(p_rhs);
                    case score_type::F64:
                        return read_as<double>(p_lhs) <=> read_as<double>(p_rhs);
                    case score_type::F128:
                        return read_as<long double>(p_lhs) <=> read_as<long double>(p_rhs);
                }
            }
            return to_long_double(p_lhs) <=> to_long_double(p_rhs);
        }

        friend constexpr bool operator==(const score &p_lhs, const score &p_rhs) noexcept {
            if (p_lhs.m_kind != p_rhs.m_kind) {
                return false;
            }
            switch (p_lhs.m_kind) {
                case score_type::I32:
                    return read_as<std::int32_t>(p_lhs) == read_as<std::int32_t>(p_rhs);
                case score_type::U_I32:
                    return read_as<std::uint32_t>(p_lhs) == read_as<std::uint32_t>(p_rhs);
                case score_type::I64:
                    return read_as<std::int64_t>(p_lhs) == read_as<std::int64_t>(p_rhs);
                case score_type::U_I64:
                    return read_as<std::uint64_t>(p_lhs) == read_as<std::uint64_t>(p_rhs);
                case score_type::F32:
                    return read_as<float>(p_lhs) == read_as<float>(p_rhs);
                case score_type::F64:
                    return read_as<double>(p_lhs) == read_as<double>(p_rhs);
                case score_type::F128:
                    return read_as<long double>(p_lhs) == read_as<long double>(p_rhs);
            }
            return false;
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
                case score_type::U_I32: {
                    uint32_t v_value{};
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
                case score_type::U_I64: {
                    uint64_t v_value{};
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
            std::ostringstream v_oss;
            switch (m_kind) {
                case score_type::I32: {
                    v_oss << get_loose<int32_t>();
                    break;
                }
                case score_type::U_I32: {
                    v_oss << get_loose<uint32_t>();
                    break;
                }
                case score_type::I64: {
                    v_oss << get_loose<int64_t>();
                    break;
                }
                case score_type::U_I64: {
                    v_oss << get_loose<uint64_t>();
                    break;
                }
                case score_type::F32: {
                    v_oss << std::setprecision(std::numeric_limits<float>::max_digits10) << get_loose<float>();
                    break;
                }
                case score_type::F64: {
                    v_oss << std::setprecision(std::numeric_limits<double>::max_digits10) << get_loose<double>();
                    break;
                }
                case score_type::F128: {
                    v_oss << std::setprecision(std::numeric_limits<long double>::max_digits10) << get_loose<long double>();
                    break;
                }
            }
            return v_oss.str();
        }

    private:
        template<typename T>
        static constexpr bool IS_SUPPORTED =
                (std::is_integral_v<std::remove_cvref_t<T>> && !std::same_as<std::remove_cvref_t<T>, bool> && (sizeof(std::remove_cvref_t<T>) == 4 || sizeof(std::remove_cvref_t<T>) == 8)) ||
                std::same_as<std::remove_cvref_t<T>, float> || std::same_as<std::remove_cvref_t<T>, double> || std::same_as<std::remove_cvref_t<T>, long double>;

        template<typename T>
        static constexpr score_type TYPE_OF = []() consteval {
            using U = std::remove_cvref_t<T>;
            if constexpr (std::is_integral_v<U> && sizeof(U) == 4 && std::is_signed_v<U>) { // NOLINT(bugprone-branch-clone)
                return score_type::I32;
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 4 && std::is_unsigned_v<U>) {
                return score_type::U_I32;
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 8 && std::is_signed_v<U>) {
                return score_type::I64;
            } else if constexpr (std::is_integral_v<U> && sizeof(U) == 8 && std::is_unsigned_v<U>) {
                return score_type::U_I64;
            } else if constexpr (std::same_as<U, float>) {
                return score_type::F32;
            } else if constexpr (std::same_as<U, double>) {
                return score_type::F64;
            } else {
                return score_type::F128;
            }
        }();

        template<typename T>
        static constexpr T read_as(const score &p_score) noexcept {
            T v_value{};
            std::memcpy(&v_value, p_score.m_payload.data(), sizeof(T));
            return v_value;
        }

        static constexpr long double to_long_double(const score &p_score) noexcept {
            switch (p_score.m_kind) {
                case score_type::I32: {
                    std::int32_t v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::U_I32: {
                    std::uint32_t v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::I64: {
                    std::int64_t v_value{};
                    std::memcpy(&v_value, p_score.m_payload.data(), sizeof(v_value));
                    return static_cast<long double>(v_value);
                }
                case score_type::U_I64: {
                    uint64_t v_value{};
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

} // namespace gempba

#endif // SCORE_HPP
