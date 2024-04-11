#ifndef GEMPBA_MPISCHEDULER_H
#define GEMPBA_MPISCHEDULER_H

#include <schedulers/Scheduler.hpp>

class MPIScheduler : public Scheduler {
private:
    MPIScheduler() {} // Private constructor for singleton

public:
    // Singleton instance creation
    static MPIScheduler &getInstance() {
        static MPIScheduler instance;
        return instance;
    }


    // Singleton instance destruction
    static void destroyInstance() {
        // No need to delete, as it's a singleton
    }

    void initialize() override {
        // MPI specific initialization
        fmt::print("Hello from MPI Scheduler");
    }

    void finalize() override {
        // MPI specific finalization
    }
};

#endif //GEMPBA_MPISCHEDULER_H
