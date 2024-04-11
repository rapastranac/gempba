#ifndef GEMPBA_UPCSCHEDULER_H
#define GEMPBA_UPCSCHEDULER_H

#include <schedulers/Scheduler.hpp>

class UPCScheduler : public Scheduler {
private:
    UPCScheduler() {} // Private constructor for singleton

public:
    // Singleton instance creation
    static Scheduler &getInstance() {
        static UPCScheduler instance;
        return instance;
    }

    // Singleton instance destruction
    static void destroyInstance() {
        // No need to delete, as it's a singleton
    }

    void initialize() override {
        fmt::print("Hello from UPC Scheduler");
    }

    void finalize() override {
        // UPC specific finalization
    }
};


#endif //GEMPBA_UPCSCHEDULER_H
