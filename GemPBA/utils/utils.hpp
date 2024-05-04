#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <fmt/format.h>

/**
 * Created by Andres Pastrana on 2024-04-08.
 */
namespace utils {

    template<typename ...T>
    void print_mpi_debug_comments(const fmt::format_string<T...> &formatString, T &&... args) {
#ifdef DEBUG_COMMENTS
        fmt::print(formatString, std::forward<T>(args)...);
#endif
    }

};

#endif //UTILS_H
