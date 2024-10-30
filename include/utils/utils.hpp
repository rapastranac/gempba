#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <any>
#include <future>

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

    template<typename T>
    static std::future<std::any> convert_to_any_future(std::future<T> &&future) {
        std::future<std::any> anyFuture = std::async(std::launch::async, [_future = std::move(future)]() mutable {
            _future.wait();
            T result = _future.get();
            return std::make_any<T>(result);
        });
        return anyFuture;
    }

};

#endif //UTILS_H
