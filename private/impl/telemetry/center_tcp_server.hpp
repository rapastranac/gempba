#ifndef GEMPBA_TELEMETRY_CENTER_TCP_SERVER_HPP
#define GEMPBA_TELEMETRY_CENTER_TCP_SERVER_HPP

#include <cstdint>
#include <memory>

namespace gempba::telemetry {

    class telemetry_hub;

    // Loopback-only TCP broadcaster. Multiple clients may connect at once and
    // all receive the same line-delimited JSON stream.
    class center_tcp_server {
    public:
        center_tcp_server(std::uint16_t p_port, telemetry_hub& p_hub) noexcept;
        ~center_tcp_server();

        center_tcp_server(const center_tcp_server&) = delete;
        center_tcp_server& operator=(const center_tcp_server&) = delete;

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

#endif // GEMPBA_TELEMETRY_CENTER_TCP_SERVER_HPP
