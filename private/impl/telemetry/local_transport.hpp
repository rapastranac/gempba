#ifndef GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP

#include <gempba/telemetry/telemetry_transport.hpp>
#include <memory>

namespace gempba::telemetry {

    // Shared-memory transport for single-process (MT-only) runs.
    class local_transport final : public telemetry_transport {
    public:
        local_transport();
        ~local_transport() override;

        void publish_worker(const worker_frame& p_frame) override;
        void publish_node(const node_frame& p_frame) override;

        void poll(const worker_frame_sink& p_worker_sink, const node_frame_sink& p_node_sink) override;

        void send_control(std::uint32_t p_dst_worker_id, const control_msg& p_msg) override;
        std::optional<control_msg> try_recv_control() override;

    private:
        struct impl;
        std::unique_ptr<impl> m_impl;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_LOCAL_TRANSPORT_HPP
