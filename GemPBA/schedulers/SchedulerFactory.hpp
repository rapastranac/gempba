#ifndef GEMPBA_SCHEDULERFACTORY_H
#define GEMPBA_SCHEDULERFACTORY_H

#include "Scheduler.hpp"
#include "UPCScheduler.hpp"
#include "MPIScheduler.hpp"

class SchedulerFactory {
public:
    enum MessagePassing {
        MPI, UPC
    };

    static Scheduler &createScheduler(MessagePassing messagePassing);

/*#ifdef USE_MPI
#elif defined(USE_UPC)
#else
    throw std::runtime_error("No Scheduler implementation was found");
#endif*/

};

static Scheduler *schedulerInstance = nullptr;

Scheduler &SchedulerFactory::createScheduler(SchedulerFactory::MessagePassing messagePassing) {
    /*TODO... only for development, for easier compilation tests, as only one MessagePassing library should be use
     * macros to be used
     * */
    switch (messagePassing) {
        case MPI: {
            schedulerInstance = &MPIScheduler::getInstance();
            return *schedulerInstance;
        }
        case UPC: {
            schedulerInstance = &UPCScheduler::getInstance();
            return *schedulerInstance;
        }
    }
}

Scheduler &Scheduler::getInstance(){
    return *schedulerInstance;
}


#endif //GEMPBA_SCHEDULERFACTORY_H
