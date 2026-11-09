#ifndef GEMPBA_TELEMETRY_RANK_FRAME_CODEC_HPP
#define GEMPBA_TELEMETRY_RANK_FRAME_CODEC_HPP

#include <cstddef>
#include <cstring>
#include <gempba/telemetry/frames.hpp>
#include <vector>

namespace gempba::telemetry::detail {

    /** @brief Wire form of @p p_frame: its @ref rank_frame_header followed by one @ref edge_counter per edge. */
    inline std::vector<std::byte> encode_rank_frame(const rank_frame& p_frame) {
        const rank_frame_header v_header(static_cast<const rank_frame_header&>(p_frame));
        const std::size_t v_edges_bytes = p_frame.m_edges_out.size() * sizeof(edge_counter);
        std::vector<std::byte> v_buffer(sizeof(rank_frame_header) + v_edges_bytes);
        std::memcpy(v_buffer.data(), &v_header, sizeof(rank_frame_header));
        if (v_edges_bytes > 0) {
            std::memcpy(v_buffer.data() + sizeof(rank_frame_header), p_frame.m_edges_out.data(), v_edges_bytes);
        }
        return v_buffer;
    }

    /** @brief Reads a frame written by @ref encode_rank_frame; @return false, leaving @p p_out untouched, when the size fits no frame. */
    inline bool decode_rank_frame(const std::byte* p_data, const std::size_t p_size, rank_frame& p_out) {
        if (p_size < sizeof(rank_frame_header) || (p_size - sizeof(rank_frame_header)) % sizeof(edge_counter) != 0) {
            return false;
        }
        rank_frame_header v_header{};
        std::memcpy(&v_header, p_data, sizeof(rank_frame_header));
        static_cast<rank_frame_header&>(p_out) = v_header;
        p_out.m_edges_out.resize((p_size - sizeof(rank_frame_header)) / sizeof(edge_counter));
        if (!p_out.m_edges_out.empty()) {
            std::memcpy(p_out.m_edges_out.data(), p_data + sizeof(rank_frame_header), p_out.m_edges_out.size() * sizeof(edge_counter));
        }
        return true;
    }

} // namespace gempba::telemetry::detail

#endif // GEMPBA_TELEMETRY_RANK_FRAME_CODEC_HPP
