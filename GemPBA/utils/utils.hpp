#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <spdlog/spdlog.h>

/**
 * Created by Andres Pastrana on 2024-04-08.
 */
namespace utils {

    template<typename ...T>
    void print_mpi_debug_comments(const fmt::format_string<T...> &formatString, T &&... args) {
#ifdef DEBUG_COMMENTS
        spdlog::info(formatString, std::forward<T>(args)...);
#endif
    }

    static void log_and_throw(const char *message) {
        spdlog::error(message);
        throw std::runtime_error(message);
    }

    static void log_and_throw(const std::string &message) {
        log_and_throw(message.c_str());
    }

};

#endif //UTILS_H
