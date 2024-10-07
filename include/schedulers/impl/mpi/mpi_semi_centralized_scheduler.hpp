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

#pragma once
#ifndef GEMPBA_MPISCHEDULER_H
#define GEMPBA_MPISCHEDULER_H

#include <spdlog/spdlog.h>

#include "branch_management/node_manager.hpp"
#include "schedulers/api/scheduler.hpp"
#include "utils/Tree.hpp"

#define TIMEOUT_TIME 3


/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class NodeManager;

    class MPISemiCentralizedScheduler final : public Scheduler {

        // Private constructor for singleton
        MPISemiCentralizedScheduler() {}

    public:
        // Singleton instance creation
        static MPISemiCentralizedScheduler *getInstance() {
            static auto *instance = new MPISemiCentralizedScheduler();
            return instance;
        }

        ~MPISemiCentralizedScheduler() override {}

        MPISemiCentralizedScheduler(const MPISemiCentralizedScheduler &) = delete;

        void operator=(const MPISemiCentralizedScheduler &) = delete;

    public:
        void runCenter(const char *seed, int count) override {
            spdlog::throw_spdlog_ex("runCenter() is not yet implemented");
        }

        void runNode(NodeManager &nodeManager) override {
            spdlog::throw_spdlog_ex("runNode() is not yet implemented");
        }

        void synchronize_statistics() override {
            spdlog::throw_spdlog_ex("synchronize_statistics() is not yet implemented");
        }

        void setLookupStrategy(LookupStrategy strategy) override {
            spdlog::throw_spdlog_ex("setLookupStrategy() is not yet implemented");
        }

        [[nodiscard]] int rank_me() const override {
            spdlog::throw_spdlog_ex("rank_me() is not yet implemented");
        }

        [[nodiscard]] int world_size() const override {
            spdlog::throw_spdlog_ex("world_size() is not yet implemented");
        }

        void barrier() override {
            spdlog::throw_spdlog_ex("barrier() is not yet implemented");
        }

        [[nodiscard]] size_t get_received_task_count(int rank) const override {
            spdlog::throw_spdlog_ex("get_received_task_count() is not yet implemented");
        }

        [[nodiscard]] size_t get_sent_task_count(int rank) const override {
            spdlog::throw_spdlog_ex("get_sent_task_count() is not yet implemented");
        }

        [[nodiscard]] double get_idle_time(int rank) const override {
            spdlog::throw_spdlog_ex("get_idle_time() is not yet implemented");
        }

        [[nodiscard]] double get_elapsed_time() const override {
            spdlog::throw_spdlog_ex("get_elapsed_time() is not yet implemented");
        }

        [[nodiscard]] size_t get_total_requested_tasks() const override {
            spdlog::throw_spdlog_ex("get_total_requested_tasks() is not yet implemented");
        }

    public:
        /**
         * enqueue a message which will be sent to the next assigned process message pushing
         * is only possible when the preceding message has been successfully pushed to
         * another process, to avoid enqueuing.
         *
         * @param message the message to be sent
         */
        void push(std::string &&message, int function_id) override {
            spdlog::throw_spdlog_ex("push() is not yet implemented");
        }

        bool try_open_transmission_channel() override {
            spdlog::throw_spdlog_ex("try_open_transmission_channel() is not yet implemented");
        }

        void close_transmission_channel() override {
            spdlog::throw_spdlog_ex("close_transmission_channel() is not yet implemented");
        }
    };
}

#endif //GEMPBA_MPISCHEDULER_H
