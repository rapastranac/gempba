#ifndef GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <gempba/telemetry/frames.hpp>
#include <optional>

namespace gempba::telemetry {

    /// Callback for one worker_frame delivered to the center role.
    using worker_frame_sink = std::function<void(std::uint32_t p_src_worker_id, const worker_frame&)>;
    /// Callback for one node_frame delivered to the center role.
    using node_frame_sink = std::function<void(std::uint32_t p_src_worker_id, const node_frame&)>;

    /**
     * @brief Abstract transport for telemetry frames between workers and the center.
     *
     * The hub owns one transport. Workers publish frames; the center role polls
     * inbound frames and drains them to the local aggregator. Backends are free
     * to drop, coalesce, or batch — the contract only requires non-blocking
     * publish and at-most-once delivery per @ref poll cycle.
     */
    class telemetry_transport {
    protected:
        telemetry_transport() = default;

    public:
        virtual ~telemetry_transport() = default;

        telemetry_transport(const telemetry_transport&) = delete;
        telemetry_transport& operator=(const telemetry_transport&) = delete;

        /**
         * @brief Publish a worker frame from the local worker. Must be non-blocking.
         *
         * Backends may drop or coalesce under back-pressure; the hot path must
         * not stall on transport state.
         */
        virtual void publish_worker(const worker_frame& p_frame) = 0;

        /// Publish a node frame from the local sentinel worker. Same non-blocking contract.
        virtual void publish_node(const node_frame& p_frame) = 0;

        /**
         * @brief Drain inbound frames received since the last call.
         *
         * Each callback fires once per frame; sinks must be re-entrant against
         * other hub state but the hub serializes @ref poll calls.
         */
        virtual void poll(const worker_frame_sink& p_worker_sink, const node_frame_sink& p_node_sink) = 0;

        /**
         * @brief Send a control message from the center to one worker.
         *
         * Used for client-pushed knobs (interval changes, sentinel promotion).
         */
        virtual void send_control(std::uint32_t p_dst_worker_id, const control_msg& p_msg) = 0;

        /**
         * @brief Pull the next inbound control message addressed to this worker.
         *
         * @return The message, or @c std::nullopt when no message is pending.
         */
        virtual std::optional<control_msg> try_recv_control() = 0;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
