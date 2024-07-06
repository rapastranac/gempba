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
        static UPCSemiCentralizedScheduler *getInstance() {
            static UPCSemiCentralizedScheduler *instance = new UPCSemiCentralizedScheduler();
            return instance;
        }

        ~UPCSemiCentralizedScheduler() override {
            finalize();
        }

        UPCSemiCentralizedScheduler(const UPCSemiCentralizedScheduler &) = delete;

        void operator=(const UPCSemiCentralizedScheduler &) = delete;

        void runCenter(const char *seed, int count) override {
            //TODO... implementation of runCenter
        }

        void runNode(BranchHandler &branchHandler) override {
            //TODO... implementation of runNode
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
