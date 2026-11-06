#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <impl/telemetry/run_id.hpp>
#include <regex>
#include <string>

namespace {

    TEST(run_id_test, format_sets_the_version_and_variant_bits) {
        EXPECT_EQ("00000000-0000-4000-8000-000000000000", gempba::telemetry::detail::format_uuid_v4({}));

        std::array<std::uint8_t, 16> v_ones{};
        v_ones.fill(0xFF);
        EXPECT_EQ("ffffffff-ffff-4fff-bfff-ffffffffffff", gempba::telemetry::detail::format_uuid_v4(v_ones));
    }

    TEST(run_id_test, format_writes_bytes_in_order) {
        const std::array<std::uint8_t, 16> v_bytes{0x12, 0x3e, 0x45, 0x67, 0xe8, 0x9b, 0x02, 0xd3, 0x24, 0x56, 0x42, 0x66, 0x14, 0x17, 0x40, 0x00};
        EXPECT_EQ("123e4567-e89b-42d3-a456-426614174000", gempba::telemetry::detail::format_uuid_v4(v_bytes));
    }

    TEST(run_id_test, generated_ids_are_uuid_v4_and_differ) {
        const std::regex v_uuid_v4{"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"};
        const std::string v_first = gempba::telemetry::detail::generate_run_id();
        const std::string v_second = gempba::telemetry::detail::generate_run_id();

        EXPECT_TRUE(std::regex_match(v_first, v_uuid_v4)) << v_first;
        EXPECT_TRUE(std::regex_match(v_second, v_uuid_v4)) << v_second;
        EXPECT_NE(v_first, v_second);
    }

} // namespace
