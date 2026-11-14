#include <cstddef>
#include <cstdint>
#include <gempba/telemetry/frames.hpp>
#include <gtest/gtest.h>
#include <impl/telemetry/rank_frame_codec.hpp>
#include <vector>

namespace {

    using gempba::telemetry::edge_counter;
    using gempba::telemetry::rank_frame;
    using gempba::telemetry::rank_frame_header;

    TEST(rank_frame_codec_test, frame_without_edges_is_only_its_header) {
        rank_frame v_frame{};
        v_frame.m_rank = 3;
        v_frame.m_tasks_sent_total = 42;
        v_frame.m_process_threads = 9;

        const std::vector<std::byte> v_wire = gempba::telemetry::detail::encode_rank_frame(v_frame);
        ASSERT_EQ(sizeof(rank_frame_header), v_wire.size());

        rank_frame v_decoded;
        ASSERT_TRUE(gempba::telemetry::detail::decode_rank_frame(v_wire.data(), v_wire.size(), v_decoded));
        EXPECT_EQ(3u, v_decoded.m_rank);
        EXPECT_EQ(42u, v_decoded.m_tasks_sent_total);
        EXPECT_EQ(9u, v_decoded.m_process_threads);
        EXPECT_TRUE(v_decoded.m_edges_out.empty());
    }

    TEST(rank_frame_codec_test, many_edges_round_trip_in_order) {
        rank_frame v_frame{};
        v_frame.m_rank = 7;
        for (std::uint32_t v_dst = 0; v_dst < 1500; v_dst += 5) {
            v_frame.m_edges_out.push_back(edge_counter{v_dst, 100ULL + v_dst, 1ULL + v_dst % 3});
        }

        const std::vector<std::byte> v_wire = gempba::telemetry::detail::encode_rank_frame(v_frame);
        ASSERT_EQ(sizeof(rank_frame_header) + v_frame.m_edges_out.size() * sizeof(edge_counter), v_wire.size());

        rank_frame v_decoded;
        ASSERT_TRUE(gempba::telemetry::detail::decode_rank_frame(v_wire.data(), v_wire.size(), v_decoded));
        EXPECT_EQ(7u, v_decoded.m_rank);
        ASSERT_EQ(v_frame.m_edges_out.size(), v_decoded.m_edges_out.size());
        for (std::size_t v_i = 0; v_i < v_frame.m_edges_out.size(); ++v_i) {
            EXPECT_EQ(v_frame.m_edges_out[v_i].m_dst_rank, v_decoded.m_edges_out[v_i].m_dst_rank);
            EXPECT_EQ(v_frame.m_edges_out[v_i].m_bytes, v_decoded.m_edges_out[v_i].m_bytes);
            EXPECT_EQ(v_frame.m_edges_out[v_i].m_count, v_decoded.m_edges_out[v_i].m_count);
        }
    }

    TEST(rank_frame_codec_test, sizes_that_fit_no_frame_are_rejected_and_leave_the_output_alone) {
        rank_frame v_frame{};
        v_frame.m_edges_out.push_back(edge_counter{1, 2, 3});
        const std::vector<std::byte> v_wire = gempba::telemetry::detail::encode_rank_frame(v_frame);

        rank_frame v_out{};
        v_out.m_rank = 99;
        EXPECT_FALSE(gempba::telemetry::detail::decode_rank_frame(v_wire.data(), sizeof(rank_frame_header) - 1, v_out));
        EXPECT_FALSE(gempba::telemetry::detail::decode_rank_frame(v_wire.data(), v_wire.size() - 1, v_out));
        EXPECT_EQ(99u, v_out.m_rank);
        EXPECT_TRUE(v_out.m_edges_out.empty());
    }

} // namespace
