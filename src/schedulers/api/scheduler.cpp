#include "schedulers/api/scheduler.hpp"
#include "schedulers/factory/scheduler_factory.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-06-30
 */
namespace gempba {
    Scheduler &Scheduler::getInstance() {
        return *SchedulerFactory::getInstance()->getSchedulerInstance();
    }
}
