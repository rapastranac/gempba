#ifndef GEMPBA_SCHEDULERFACTORY_HPP
#define GEMPBA_SCHEDULERFACTORY_HPP

#include "spdlog/spdlog.h"

#include "schedulers/api/scheduler.hpp"
#include "schedulers/impl/upc/upc_semi_centralized_scheduler.hpp"
#include "schedulers/impl/mpi/mpi_semi_centralized_scheduler.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-05-05
 */
namespace gempba {

    class SchedulerFactory {
        Scheduler *schedulerInstance;

        SchedulerFactory() = default;

    public:

        SchedulerFactory(const SchedulerFactory &) = delete;

        void operator=(const SchedulerFactory &) = delete;

        ~SchedulerFactory() {
            delete schedulerInstance;
        }

        static SchedulerFactory *getInstance() {
            static auto *instance = new SchedulerFactory();
            return instance;
        }

        Scheduler *getSchedulerInstance() {
            if (schedulerInstance == nullptr) {
                const char *msg = "Scheduler instance not created yet";
                spdlog::critical(msg);
                throw std::runtime_error(msg);
            }
            return schedulerInstance;
        }


        Scheduler &createScheduler(InterprocessProvider provider, Topology topology) {
            if (schedulerInstance != nullptr) {
                const char *msg = "Scheduler instance already created";
                spdlog::critical(msg);
                throw std::runtime_error(msg);
            }

            switch (provider) {
                case MPI: {
                    switch (topology) {
                        case SEMI_CENTRALIZED: {
                            schedulerInstance = MPISemiCentralizedScheduler::getInstance();
                            return *schedulerInstance;
                        }
                        case CENTRALIZED: {
                            spdlog::throw_spdlog_ex("Centralized topology not implemented for MPI");
                        }
                        default: {
                            spdlog::throw_spdlog_ex("Invalid topology");
                        }
                    }
                }
                case UPC: {
                    switch (topology) {
                        case SEMI_CENTRALIZED: {
                            schedulerInstance = UPCSemiCentralizedScheduler::getInstance();
                            return *schedulerInstance;
                        }
                        case CENTRALIZED: {
                            spdlog::throw_spdlog_ex("Centralized topology not implemented for UPC");
                        }
                        default: {
                            spdlog::throw_spdlog_ex("Invalid topology");
                        }
                    }
                }
                default: {
                    spdlog::throw_spdlog_ex("Invalid provider");
                }
            }
        }
    };
}
#endif //GEMPBA_SCHEDULERFACTORY_HPP