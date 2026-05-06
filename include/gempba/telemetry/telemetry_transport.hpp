#ifndef GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <gempba/telemetry/frames.hpp>
#include <optional>

namespace gempba::telemetry {

    using worker_frame_sink = std::function<void(std::uint32_t p_src_worker_id, const worker_frame&)>;
    using node_frame_sink = std::function<void(std::uint32_t p_src_worker_id, const node_frame&)>;

    class telemetry_transport {
    protected:
        telemetry_transport() = default;

    public:
        virtual ~telemetry_transport() = default;

        telemetry_transport(const telemetry_transport&) = delete;
        telemetry_transport& operator=(const telemetry_transport&) = delete;

        // Worker-side push. Must be non-blocking; backends may drop or coalesce.
        virtual void publish_worker(const worker_frame& p_frame) = 0;
        virtual void publish_node(const node_frame& p_frame) = 0;

        // Center-side drain. Sinks fire once per frame received since last call.
        virtual void poll(const worker_frame_sink& p_worker_sink, const node_frame_sink& p_node_sink) = 0;

        virtual void send_control(std::uint32_t p_dst_worker_id, const control_msg& p_msg) = 0;
        virtual std::optional<control_msg> try_recv_control() = 0;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
