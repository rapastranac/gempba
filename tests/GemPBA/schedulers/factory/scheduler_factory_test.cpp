#include <gtest/gtest.h>
#include "../../../../GemPBA/schedulers/factory/scheduler_factory.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-06-30
 */

TEST(SchedulerFactoryTest, test_create_mpi_semi_centralized_scheduler) {
    // Create a SchedulerFactory object
    gempba::SchedulerFactory &factory = gempba::SchedulerFactory::getInstance();

    // Create a Scheduler object using the SchedulerFactory
    gempba::Scheduler &scheduler = factory.createScheduler(gempba::InterprocessProvider::MPI, gempba::Topology::SEMI_CENTRALIZED);

    // Check if the created Scheduler object is not null
    ASSERT_NE(&scheduler, nullptr);

    // Check if the created Scheduler object is of the correct type
    ASSERT_TRUE(dynamic_cast<gempba::MPISemiCentralizedScheduler *>(&scheduler) != nullptr);

}