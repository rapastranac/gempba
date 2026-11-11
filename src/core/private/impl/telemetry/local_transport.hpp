#ifndef GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP

#include <impl/telemetry/telemetry_transport.hpp>
#include <memory>

namespace gempba::telemetry {

    /**
     * @brief In-process @ref telemetry_transport for single-process runs.
     *
     * The sending side and the aggregator live in the same address space, so frames
     * are handed off through shared queues — no socket, no serialization.
     */
    class local_transport final : public telemetry_transport {
    public:
        local_transport();
        ~local_transport() override;

        void publish_rank(const rank_frame& p_frame) override;
        void publish_node(const node_frame& p_frame) override;

        void poll(const rank_frame_sink& p_rank_sink, const node_frame_sink& p_node_sink) override;

        void send_control(std::uint32_t p_dst_rank, const control_msg& p_msg) override;
        std::optional<control_msg> try_recv_control() override;
        [[nodiscard]] std::vector<std::vector<std::byte>> all_gather(std::span<const std::byte> p_block) override;

    private:
        struct impl;
        std::unique_ptr<impl> m_impl;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP
