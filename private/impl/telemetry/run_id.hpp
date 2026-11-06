#ifndef GEMPBA_TELEMETRY_RUN_ID_HPP
#define GEMPBA_TELEMETRY_RUN_ID_HPP

#include <array>
#include <chrono>
#include <cstdint>
#include <random>
#include <string>

namespace gempba::telemetry::detail {

    /** @brief Formats @p p_bytes as a lowercase RFC 4122 version 4 UUID, overwriting its version and variant bits. */
    inline std::string format_uuid_v4(std::array<std::uint8_t, 16> p_bytes) {
        p_bytes[6] = static_cast<std::uint8_t>((p_bytes[6] & 0x0F) | 0x40);
        p_bytes[8] = static_cast<std::uint8_t>((p_bytes[8] & 0x3F) | 0x80);

        constexpr char k_hex[] = "0123456789abcdef";
        std::string v_out;
        v_out.reserve(36);
        for (std::size_t v_i = 0; v_i < p_bytes.size(); ++v_i) {
            if (v_i == 4 || v_i == 6 || v_i == 8 || v_i == 10) {
                v_out += '-';
            }
            v_out += k_hex[p_bytes[v_i] >> 4];
            v_out += k_hex[p_bytes[v_i] & 0x0F];
        }
        return v_out;
    }

    /**
     * @brief A random UUID v4 identifying one gempba run.
     *
     * The clock joins @c std::random_device in the seed because some MinGW runtimes implement the device with a fixed sequence.
     */
    inline std::string generate_run_id() {
        std::random_device v_device;
        const auto v_now = static_cast<std::uint64_t>(std::chrono::system_clock::now().time_since_epoch().count());
        std::seed_seq v_seed{v_device(), v_device(), v_device(), v_device(), static_cast<std::uint32_t>(v_now), static_cast<std::uint32_t>(v_now >> 32)};
        std::mt19937_64 v_engine(v_seed);

        std::array<std::uint8_t, 16> v_bytes{};
        for (std::size_t v_i = 0; v_i < v_bytes.size(); v_i += 8) {
            const std::uint64_t v_word = v_engine();
            for (std::size_t v_j = 0; v_j < 8; ++v_j) {
                v_bytes[v_i + v_j] = static_cast<std::uint8_t>(v_word >> (8 * v_j));
            }
        }
        return format_uuid_v4(v_bytes);
    }

} // namespace gempba::telemetry::detail

#endif // GEMPBA_TELEMETRY_RUN_ID_HPP
