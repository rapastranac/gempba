/*
 * MIT License
 *
 * Copyright (c) 2024. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#ifndef UTILS_H
#define UTILS_H

#include <any>
#include <cfloat>
#include <future>
#include <spdlog/spdlog.h>
#include <sys/time.h>
#include <gempba/utils/gempba_utils.hpp>
#include <gempba/utils/tree.hpp>
#include <gempba/utils/score.hpp>



/**
 * Created by Andrés Pastrana on 2024-04-08.
 */
namespace utils {
    template<typename... T>
    void print_ipc_debug_comments(const fmt::format_string<T...> &p_format_string, T &&... p_args) {
        #if GEMPBA_DEBUG_COMMENTS
        spdlog::debug(p_format_string, std::forward<T>(p_args)...);
        #endif
    }

    inline void log_and_throw(const std::string &v_message) {
        spdlog::error(v_message);
        throw std::runtime_error(v_message);
    }

    template<typename... T>
    void log_and_throw(const fmt::format_string<T...> &p_format_string, T &&... p_args) {
        const std::string v_message = fmt::format(p_format_string, std::forward<T>(p_args)...);
        log_and_throw(v_message);
    }

    template<typename T>
    std::future<std::any> convert_to_any_future(std::future<T> &&p_future) {
        std::future<std::any> any_future = std::async(std::launch::async, [fut = std::move(p_future)]() mutable {
            fut.wait();
            T result = fut.get();
            return std::make_any<T>(result);
        });
        return any_future;
    }

    // already adapted for multi-branching
    static int get_next_child(const int p_child, const int p_parent, const int p_children_per_node, const int p_depth) {
        return p_child * static_cast<int>(pow(p_children_per_node, p_depth)) + p_parent;
    }

    static void build_topology(tree &p_tree, int p_parent, const int p_depth_start, const int p_children_per_node, const int p_total) {
        for (int depth = p_depth_start; depth < log2(p_total); depth++) {
            for (int child = 1; child < p_children_per_node; child++) {
                int next_child = get_next_child(child, p_parent, p_children_per_node, depth);
                if (next_child >= p_total || next_child <= 0) {
                    continue;
                }
                p_tree[p_parent].add_next(next_child);
                spdlog::debug("process: {}, child: {}\n", p_parent, next_child);
                build_topology(p_tree, next_child, depth + 1, p_children_per_node, p_total);
            }
        }
    }

    static double diff_time(const double w_time0, const double w_time1) { return w_time1 - w_time0; }

    /**
     * @brief Shifts elements of the vector to the left by one position.
     *
     * Moves each element of the vector one position to the left, stopping when the first
     * -1 is encountered. Assumes the vector is either fully populated or has -1 values
     * on the right side.
     *
     * @param vector_ The vector to be shifted. It should have been initialized with at least
     *            the required number of elements.
     */
    static void shift_left(std::vector<int> &vector_) {
        const int size = static_cast<int>(vector_.size());
        if (size == 0) {
            utils::log_and_throw("Attempted to shift an empty vector");
        }
        for (int i = 0; i < size - 1; i++) {
            if (vector_[i] != -1) {
                vector_[i] = vector_[i + 1]; // shift one cell to the left
                if (i == size - 2 && vector_[i] != -1) {
                    vector_[i + 1] = -1; // set the last cell to -1
                }
            } else {
                break; // Stop if the first -1 is encountered
            }
        }
    }

    static double wall_time() {
        timeval time{};
        if (gettimeofday(&time, nullptr)) {
            return -1.0;
        }
        return static_cast<double>(time.tv_sec) + static_cast<double>(time.tv_usec) * .000001;
    }


    static gempba::score get_default_score(const gempba::goal p_goal, const gempba::score_type p_type) {
        switch (p_type) {
            case gempba::score_type::I32: {
                if (p_goal == gempba::MAXIMISE) {
                    return gempba::score::make(INT_MIN); // maximisation
                }
                return gempba::score::make(INT_MAX); // minimisation
            }
            case gempba::score_type::I64: {
                if (p_goal == gempba::MAXIMISE) {
                    return gempba::score::make(LONG_MIN); // maximisation
                }
                return gempba::score::make(LONG_MAX); // minimisation
            }
            case gempba::score_type::F32: {
                if (p_goal == gempba::MAXIMISE) {
                    return gempba::score::make(FLT_MIN); // maximisation
                }
                return gempba::score::make(FLT_MAX); // minimisation
            }
            case gempba::score_type::F64: {
                if (p_goal == gempba::MAXIMISE) {
                    return gempba::score::make(DBL_MIN); // maximisation
                }
                return gempba::score::make(DBL_MAX); // minimisation
            }
            case gempba::score_type::F128: {
                if (p_goal == gempba::MAXIMISE) {
                    return gempba::score::make(LDBL_MIN); // maximisation
                }
                return gempba::score::make(LDBL_MAX); // minimisation
            }
            default: {
                utils::log_and_throw("Invalid score type: {}", static_cast<int>(p_type));
            }
        }
    }
}; // namespace utils

#endif // UTILS_H
