#pragma once
#ifndef UTILS_H
#define UTILS_H

#include <any>
#include <future>

#include <spdlog/spdlog.h>

#include "Tree.hpp"

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

    // already adapted for multi-branching
    static int get_next_child(int child, int parent, int children_per_node, int depth) {
        return child * (int) pow(children_per_node, depth) + parent;
    }

    static void build_topology(Tree &tree, int parent, int depth_start, int children_per_node, int total) {
        for (int depth = depth_start; depth < log2(total); depth++) {
            for (int child = 1; child < children_per_node; child++) {
                int next_child = get_next_child(child, parent, children_per_node, depth);
                if (next_child >= total || next_child <= 0) {
                    continue;
                }
                tree[parent].addNext(next_child);
                spdlog::info("process: {}, child: {}\n", parent, next_child);
                build_topology(tree, next_child, depth + 1, children_per_node, total);
            }
        }
    }
};

#endif //UTILS_H
