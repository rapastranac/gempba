#pragma once
#ifndef UTILS_H
#define UTILS_H

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

    static double difftime(double wTime0, double wTime1) {
        return wTime1 - wTime0;
    }


    static void log_and_throw(const char *message) {
        spdlog::error(message);
        throw std::runtime_error(message);
    }

    static void log_and_throw(const std::string &message) {
        log_and_throw(message.c_str());
    }

    /**
        * shift a position to left of an array, leaving -1 as default value
        * @param array
        * @param size
        */
    static void shift_left(int array[], const int size) {
        for (int i = 0; i < size - 1; i++) {
            if (array[i] != -1) {
                array[i] = array[i + 1]; // shift one cell to the left
            } else {
                break; // no more data
            }
        }
    }

};

#endif //UTILS_H
