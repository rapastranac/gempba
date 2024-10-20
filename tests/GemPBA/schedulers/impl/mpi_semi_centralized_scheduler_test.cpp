#include <gtest/gtest.h>
#include "../../../../GemPBA/schedulers/factory/scheduler_factory.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-06-30
 */

TEST(SemiCentralizedSchedulerTest, test_semi_centralized_scheduler_creation) {

    // Create a semi-centralized scheduler
    gempba::Scheduler* scheduler = gempba::MPISemiCentralizedScheduler::getInstance();

    // Check if the semi-centralized scheduler is created

}