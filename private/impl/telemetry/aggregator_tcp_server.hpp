#ifndef GEMPBA_TELEMETRY_AGGREGATOR_TCP_SERVER_HPP
#define GEMPBA_TELEMETRY_AGGREGATOR_TCP_SERVER_HPP

#include <cstdint>
#include <memory>

namespace gempba::telemetry {

    class telemetry_hub;

    /**
     * @brief Loopback-only TCP broadcaster for the aggregator's dashboard channel.
     *
     * Accepts multiple concurrent clients; each receives the same line-delimited
     * JSON stream. Lifetime is owned by the @ref telemetry_hub.
     */
    class aggregator_tcp_server {
    public:
        aggregator_tcp_server(std::uint16_t p_port, telemetry_hub& p_hub) noexcept;
        ~aggregator_tcp_server();

        aggregator_tcp_server(const aggregator_tcp_server&) = delete;
        aggregator_tcp_server& operator=(const aggregator_tcp_server&) = delete;

        // Returns false on bind failure; stop() / destruction stay safe.
        bool start();
        void stop();

        // bound_port lets tests pass port 0 and read back the OS-assigned one.
        [[nodiscard]] std::uint16_t bound_port() const noexcept;

    private:
        struct impl;
        std::unique_ptr<impl> m_impl;
    };

} // namespace gempba::telemetry

#endif // GEMPBA_TELEMETRY_AGGREGATOR_TCP_SERVER_HPP
