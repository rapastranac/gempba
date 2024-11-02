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