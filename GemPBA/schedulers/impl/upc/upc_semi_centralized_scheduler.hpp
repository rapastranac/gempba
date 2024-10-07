#ifndef GEMPBA_UPCSCHEDULER_H
#define GEMPBA_UPCSCHEDULER_H

#include <spdlog/spdlog.h>
#include <stdexcept>
#include "schedulers/api/scheduler.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    /**
     * incomplete implementation of UPC Scheduler. This is supposed to add UPC++ specific functionalities.
     */
    class UPCSemiCentralizedScheduler final : public Scheduler {

        // TODO... members variables in here

        // Private constructor for singleton
        UPCSemiCentralizedScheduler() = default;

    public:
        // Singleton instance creation
        static UPCSemiCentralizedScheduler *getInstance() {
            static auto *instance = new UPCSemiCentralizedScheduler();
            return instance;
        }

        ~UPCSemiCentralizedScheduler() override = default;

        UPCSemiCentralizedScheduler(const UPCSemiCentralizedScheduler &) = delete;

        void operator=(const UPCSemiCentralizedScheduler &) = delete;


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

#endif //GEMPBA_UPCSCHEDULER_H
