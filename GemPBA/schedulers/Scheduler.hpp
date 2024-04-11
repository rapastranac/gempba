#ifndef GEMPBA_SCHEDULER_H
#define GEMPBA_SCHEDULER_H

#include "fmt/core.h"

#include "SchedulerFactory.hpp"

/*
 * author: Andres Pastrana
 * email: rapastranac@gmail.com
 */

class Scheduler {
public:
    // Singleton instance creation
    static Scheduler &getInstance();

    virtual void initialize() = 0;

    virtual void finalize() = 0;


    virtual ~Scheduler() = default; // Virtual destructor for polymorphic behaviour
protected:
    //  TODO...
private:
    //  TODO...
};

/*#ifdef USE_MPI

#include <schedulers/MPIScheduler.hpp>

using SelectedScheduler = MPIScheduler;
#elif defined(USE_UPC)
#include <schedulers/UPCScheduler.hpp>
using SelectedScheduler = UPCScheduler;
#endif*/

#endif //GEMPBA_SCHEDULER_H
