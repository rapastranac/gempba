#include <gempba/gempba.hpp>
#include <gempba/mpi.hpp>
#include <impl/install_scheduler.hpp>
#include <impl/schedulers/mpi_centralized_scheduler.hpp>
#include <impl/schedulers/mpi_semi_centralized_scheduler.hpp>
#include <impl/telemetry/mpi_transport.hpp>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
    std::unique_ptr<gempba::telemetry::telemetry_transport> make_mpi_transport() { return std::make_unique<gempba::telemetry::mpi_transport>(); }
} // namespace

gempba::scheduler* gempba::mpi::create_scheduler(std::unique_ptr<scheduler> p_your_implementation) { return detail::install_scheduler(std::move(p_your_implementation), make_mpi_transport); }

gempba::scheduler* gempba::mpi::create_scheduler(const scheduler_topology& p_topology) {
    if (try_get_scheduler() != nullptr) {
        throw std::runtime_error("scheduler already exists!");
    }
    std::unique_ptr<scheduler> v_scheduler;
    switch (p_topology) {
        case SEMI_CENTRALIZED: {
            v_scheduler = std::make_unique<mpi_semi_centralized_scheduler>();
            break;
        }
        case CENTRALIZED: {
            v_scheduler = std::make_unique<mpi_centralized_scheduler>();
            break;
        }
    }
    return detail::install_scheduler(std::move(v_scheduler), make_mpi_transport);
}

std::unique_ptr<gempba::default_mpi_stats_visitor> gempba::mpi::get_default_mpi_stats_visitor() { return std::make_unique<default_mpi_stats_visitor>(); }
