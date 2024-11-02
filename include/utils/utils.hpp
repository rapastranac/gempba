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


    static double diff_time(double wTime0, double wTime1) {
        return wTime1 - wTime0;
    }

    /**
    * @brief Shifts elements of the vector to the left by one position.
    *
    * Moves each element of the vector one position to the left, stopping when the first
    * -1 is encountered. Assumes the vector is either fully populated or has -1 values
    * on the right side.
    *
    * @param vec The vector to be shifted. It should have been initialized with at least
    *            the required number of elements.
    */
    static void shift_left(std::vector<int> &vector) {
        int size = static_cast<int>(vector.size());
        if (size == 0) {
            spdlog::throw_spdlog_ex("Attempted to shift an empty vector");
        }
        for (int i = 0; i < size - 1; i++) {
            if (vector[i] != -1) {
                vector[i] = vector[i + 1]; // shift one cell to the left
                if (i == size - 2 && vector[i] != -1) {
                    vector[i + 1] = -1; // set the last cell to -1
                }
            } else {
                break; // Stop if the first -1 is encountered
            }
        }
    }


};

#endif //UTILS_H
