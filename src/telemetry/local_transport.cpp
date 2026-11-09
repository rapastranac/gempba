#include <impl/telemetry/local_transport.hpp>

#include <array>
#include <deque>
#include <mutex>
#include <utility>

namespace gempba::telemetry {

    struct local_transport::impl {
        struct rank_slot {
            std::mutex m_mtx;
            rank_frame m_frame{};
            bool m_dirty = false;
        };

        struct node_slot {
            std::mutex m_mtx;
            node_frame m_frame{};
            bool m_dirty = false;
        };

        std::array<rank_slot, MAX_RANKS> m_rank_slots;
        std::array<node_slot, MAX_RANKS> m_node_slots;

        std::mutex m_control_mtx;
        std::deque<std::pair<std::uint32_t, control_msg>> m_control_queue;
    };

    local_transport::local_transport() : m_impl(std::make_unique<impl>()) {}

    local_transport::~local_transport() = default;

    void local_transport::publish_rank(const rank_frame& p_frame) {
        if (p_frame.m_rank >= MAX_RANKS)
            return;
        auto& v_slot = m_impl->m_rank_slots[p_frame.m_rank];
        const std::scoped_lock v_lock(v_slot.m_mtx);
        v_slot.m_frame = p_frame;
        v_slot.m_dirty = true;
    }

    void local_transport::publish_node(const node_frame& p_frame) {
        if (p_frame.m_sentinel_rank >= MAX_RANKS)
            return;
        auto& v_slot = m_impl->m_node_slots[p_frame.m_sentinel_rank];
        const std::scoped_lock v_lock(v_slot.m_mtx);
        v_slot.m_frame = p_frame;
        v_slot.m_dirty = true;
    }

    void local_transport::poll(const rank_frame_sink& p_rank_sink, const node_frame_sink& p_node_sink) {
        for (std::uint32_t v_i = 0; v_i < MAX_RANKS; ++v_i) {
            auto& v_slot = m_impl->m_rank_slots[v_i];
            rank_frame v_local{};
            bool v_was_dirty = false;
            {
                const std::scoped_lock v_lock(v_slot.m_mtx);
                if (v_slot.m_dirty) {
                    v_local = v_slot.m_frame;
                    v_slot.m_dirty = false;
                    v_was_dirty = true;
                }
            }
            if (v_was_dirty && p_rank_sink) {
                p_rank_sink(v_i, v_local);
            }
        }
        for (std::uint32_t v_i = 0; v_i < MAX_RANKS; ++v_i) {
            auto& v_slot = m_impl->m_node_slots[v_i];
            node_frame v_local{};
            bool v_was_dirty = false;
            {
                const std::scoped_lock v_lock(v_slot.m_mtx);
                if (v_slot.m_dirty) {
                    v_local = v_slot.m_frame;
                    v_slot.m_dirty = false;
                    v_was_dirty = true;
                }
            }
            if (v_was_dirty && p_node_sink) {
                p_node_sink(v_i, v_local);
            }
        }
    }

    void local_transport::send_control(const std::uint32_t p_dst_rank, const control_msg& p_msg) {
        const std::scoped_lock v_lock(m_impl->m_control_mtx);
        m_impl->m_control_queue.emplace_back(p_dst_rank, p_msg);
    }

    std::optional<control_msg> local_transport::try_recv_control() {
        const std::scoped_lock v_lock(m_impl->m_control_mtx);
        if (m_impl->m_control_queue.empty())
            return std::nullopt;
        control_msg v_msg = m_impl->m_control_queue.front().second;
        m_impl->m_control_queue.pop_front();
        return v_msg;
    }

} // namespace gempba::telemetry
