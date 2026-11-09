#ifndef GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
#define GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP

#include <cstdint>
#include <functional>
#include <gempba/telemetry/frames.hpp>
#include <optional>

namespace gempba::telemetry {

    /// Callback for one rank_frame delivered to the aggregator.
    using rank_frame_sink = std::function<void(std::uint32_t p_src_rank, const rank_frame&)>;
    /// Callback for one node_frame delivered to the aggregator.
    using node_frame_sink = std::function<void(std::uint32_t p_src_rank, const node_frame&)>;

    /**
     * @brief Abstract transport for telemetry frames between ranks and the aggregator.
     *
     * The hub owns one transport. Every rank publishes frames; the aggregator polls
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
        virtual void publish_worker(const rank_frame& p_frame) = 0;

        /// Publish a node frame from the local sentinel worker. Same non-blocking contract.
        virtual void publish_node(const node_frame& p_frame) = 0;

        /**
         * @brief Publish the local worker's last frame before shutdown, so the aggregator can tell it apart.
         *
         * Defaults to @ref publish_worker for backends where every frame reaches the aggregator before shutdown.
         */
        virtual void publish_final_worker(const rank_frame& p_frame) { publish_worker(p_frame); }

        /// @return How many final worker frames @ref poll has delivered to the aggregator so far.
        [[nodiscard]] virtual std::uint32_t final_frames_received() const { return 0; }

        /**
         * @brief Drain inbound frames received since the last call.
         *
         * Each callback fires once per frame; sinks must be re-entrant against
         * other hub state but the hub serializes @ref poll calls.
         */
        virtual void poll(const rank_frame_sink& p_rank_sink, const node_frame_sink& p_node_sink) = 0;

        /**
         * @brief Send a control message from the aggregator to one rank.
         *
         * Used for client-pushed knobs (interval changes, sentinel promotion).
         */
        virtual void send_control(std::uint32_t p_dst_rank, const control_msg& p_msg) = 0;

        /**
         * @brief Pull the next inbound control message addressed to this worker.
         *
         * @return The message, or @c std::nullopt when no message is pending.
         */
        virtual std::optional<control_msg> try_recv_control() = 0;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_TELEMETRY_TRANSPORT_HPP
