#ifndef GEMPBA_UPCSCHEDULER_H
#define GEMPBA_UPCSCHEDULER_H

#include <spdlog/spdlog.h>
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
        UPCSemiCentralizedScheduler() {
            initialize();
        }

    public:
        // Singleton instance creation
        static UPCSemiCentralizedScheduler &getInstance() {
            static UPCSemiCentralizedScheduler instance;
            return instance;
        }

        ~UPCSemiCentralizedScheduler() override {
            finalize();
        }

        UPCSemiCentralizedScheduler(const UPCSemiCentralizedScheduler &) = delete;

        void operator=(const UPCSemiCentralizedScheduler &) = delete;

        void runCenter(const char *seed, int count) override {
            throw std::runtime_error("Not implemented yet");
        }

        void runNode(BranchHandler &branchHandler) override {
            throw std::runtime_error("Not implemented yet");
        }

        void synchronize_statistics() override {
            throw std::runtime_error("Not implemented yet");
        }

        void setLookupStrategy(LookupStrategy strategy) override {
            throw std::runtime_error("Not implemented yet");
        }

        int rank_me() const override {
            throw std::runtime_error("Not implemented yet");
        }

        int world_size() const override {
            throw std::runtime_error("Not implemented yet");
        }

        void barrier() override {
            throw std::runtime_error("Not implemented yet");
        }

        size_t get_received_task_count(int rank) const override {
            throw std::runtime_error("Not implemented yet");
        }

        size_t get_sent_task_count(int rank) const override {
            throw std::runtime_error("Not implemented yet");
        }

        double get_elapsed_time(int rank) const override {
            throw std::runtime_error("Not implemented yet");
        }

        double get_idle_time(int rank) const override {
            throw std::runtime_error("Not implemented yet");
        }

        size_t get_total_requested_tasks() const override {
            throw std::runtime_error("Not implemented yet");
        }

        void push(std::string &&message, int function_id) override {
            throw std::runtime_error("Not implemented yet");
        }

        bool try_open_transmission_channel() override {
            throw std::runtime_error("Not implemented yet");
        }

        void close_transmission_channel() override {
            throw std::runtime_error("Not implemented yet");
        }

    private:

        static void initialize() {
            // UPC specific initialization
            spdlog::info("Hello from UPC Scheduler\n");
        }

        static void finalize() {
            // UPC specific finalization
            spdlog::info("Goodbye from UPC Scheduler\n");
        }

    };
}

#endif //GEMPBA_UPCSCHEDULER_H
