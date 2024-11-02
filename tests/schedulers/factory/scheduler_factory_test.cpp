#include <gtest/gtest.h>
#include "schedulers/factory/scheduler_factory.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-06-30
 */


class SchedulerFactoryTest : public testing::Test {
public:
    void SetUp() override {
        // Create a SchedulerFactory object
        factory = gempba::SchedulerFactory::getInstance();

        // Create a Scheduler object using the SchedulerFactory
        scheduler = &factory->createScheduler(gempba::InterprocessProvider::MPI, gempba::Topology::SEMI_CENTRALIZED);
    }

    void TearDown() override {
        delete factory;
    }

    gempba::SchedulerFactory *factory = nullptr;
    gempba::Scheduler *scheduler = nullptr;
};

TEST_F(SchedulerFactoryTest, test_create_mpi_semi_centralized_scheduler) {

    // Check if the created Scheduler object is not null
    ASSERT_NE(scheduler, nullptr);

    // Check if the created Scheduler object is of the correct type
    ASSERT_TRUE(dynamic_cast<gempba::MPISemiCentralizedScheduler *>(scheduler) != nullptr);

}