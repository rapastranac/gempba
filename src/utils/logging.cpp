#include <gempba/utils/utils.hpp>
#include <spdlog/spdlog.h>

namespace utils {

    void vlog_debug(const std::string_view p_format, const std::format_args p_args) {
        if (spdlog::should_log(spdlog::level::debug)) {
            spdlog::debug("{}", std::vformat(p_format, p_args));
        }
    }

    void log_error(const std::string_view p_message) { spdlog::error("{}", p_message); }

} // namespace utils
