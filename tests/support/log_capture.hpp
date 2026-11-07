#ifndef GEMPBA_TESTS_LOG_CAPTURE_HPP
#define GEMPBA_TESTS_LOG_CAPTURE_HPP

#include <memory>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

/**
 * @brief Routes spdlog's default logger, which gempba logs through, to a string at debug level while it lives.
 *
 * Each line is the level followed by the message, for example "[debug] rank 0". The previous default logger comes back
 * when the capture is destroyed.
 */
class log_capture {
public:
    log_capture() : m_previous(spdlog::default_logger()) {
        auto v_logger = std::make_shared<spdlog::logger>(k_name, std::make_shared<spdlog::sinks::ostream_sink_mt>(m_stream));
        v_logger->set_level(spdlog::level::debug);
        v_logger->set_pattern("[%l] %v");
        spdlog::set_default_logger(v_logger);
    }

    ~log_capture() {
        spdlog::set_default_logger(m_previous);
        spdlog::drop(k_name);
    }

    log_capture(const log_capture&) = delete;
    log_capture& operator=(const log_capture&) = delete;

    void set_level(const spdlog::level::level_enum p_level) { spdlog::default_logger()->set_level(p_level); }

    [[nodiscard]] std::string text() const { return m_stream.str(); }

private:
    static constexpr auto k_name = "log_capture";
    std::ostringstream m_stream;
    std::shared_ptr<spdlog::logger> m_previous;
};

#endif // GEMPBA_TESTS_LOG_CAPTURE_HPP
