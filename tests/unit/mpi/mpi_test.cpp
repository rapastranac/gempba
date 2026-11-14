#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include <gempba/cabi/gempba_mpi.h>
#include <gempba/gempba.hpp>
#include <gempba/mpi.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <scheduler_mock.hpp>

namespace {
    using scheduler_double = ::testing::NiceMock<gempba::testing_support::scheduler_mock>;

    class mpi_test : public ::testing::Test {
    protected:
        void TearDown() override {
            gempba::telemetry::uninstall();
            gempba::reset_scheduler();
        }
    };

    TEST_F(mpi_test, create_scheduler_keeps_a_custom_implementation) {
        auto v_custom = std::make_unique<scheduler_double>();
        const gempba::scheduler* v_raw = v_custom.get();

        gempba::scheduler* v_scheduler = gempba::mpi::create_scheduler(std::move(v_custom));

        EXPECT_EQ(v_raw, v_scheduler);
        EXPECT_EQ(v_scheduler, gempba::get_scheduler());
    }

    TEST_F(mpi_test, create_scheduler_throws_if_one_already_exists) {
        gempba::mpi::create_scheduler(std::make_unique<scheduler_double>());

        EXPECT_THROW(gempba::mpi::create_scheduler(std::make_unique<scheduler_double>()), std::runtime_error);
        EXPECT_THROW(gempba::mpi::create_scheduler(gempba::mpi::CENTRALIZED), std::runtime_error);
    }

    TEST_F(mpi_test, get_default_mpi_stats_visitor_returns_non_null) { EXPECT_NE(nullptr, gempba::mpi::get_default_mpi_stats_visitor()); }

    TEST(mpi_cabi_test, create_scheduler_rejects_a_null_out_through_the_cores_last_error) {
        EXPECT_EQ(GEMPBA_ERR_INVALID_ARG, gempba_mpi_create_scheduler(GEMPBA_TOPOLOGY_CENTRALIZED, /*out=*/nullptr));
        EXPECT_NE(gempba_last_error_message(), nullptr);
    }
} // namespace
