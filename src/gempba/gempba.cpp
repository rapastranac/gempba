#include <memory>
#include <gempba/gempba.hpp>
#include <gempba/branch_handler.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>
#include <impl/schedulers/mpi_centralized_scheduler.hpp>
#include <impl/schedulers/mpi_semi_centralized_scheduler.hpp>

namespace {
    inline std::unique_ptr<gempba::scheduler> g_scheduler;
    inline std::unique_ptr<gempba::load_balancer> g_load_balancer;
    inline std::unique_ptr<gempba::branch_handler> g_branch_handler;
}

void gempba::check_not_null([[maybe_unused]] const node &p_parent) {
    if (p_parent == nullptr) {
        spdlog::throw_spdlog_ex("Node creation cannot have a nullptr for a parent");
    }
}

gempba::load_balancer *gempba::mt::create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation) {
    if (g_load_balancer != nullptr) {
        throw std::runtime_error("load_balancer already exists!");
    }
    g_load_balancer = std::move(p_your_implementation);
    return g_load_balancer.get();
}

gempba::load_balancer *gempba::mt::create_load_balancer(const balancing_policy &p_policy) {
    return mp::create_load_balancer(p_policy, nullptr);
}

gempba::branch_handler &gempba::mt::create_branch_handler(load_balancer *p_load_balancer) {
    return mp::create_branch_handler(p_load_balancer, nullptr);
}

gempba::scheduler *gempba::mp::create_scheduler(std::unique_ptr<scheduler> p_your_implementation) {
    if (g_scheduler != nullptr) {
        throw std::runtime_error("load_balancer already exists!");
    }
    g_scheduler = std::move(p_your_implementation);
    return g_scheduler.get();
}

gempba::scheduler *gempba::mp::create_scheduler(const scheduler_topology &p_topology, const double p_timeout) {
    if (g_scheduler != nullptr) {
        throw std::runtime_error("scheduler already exists!");
    }
    switch (p_topology) {
        case SEMI_CENTRALIZED: {
            g_scheduler = std::unique_ptr<scheduler>(new mpi_semi_centralized_scheduler(p_timeout));
            break;
        }
        case CENTRALIZED: {
            g_scheduler = std::unique_ptr<scheduler>(new mpi_centralized_scheduler(p_timeout));
            break;
        }
    }
    return g_scheduler.get();
}

gempba::load_balancer *gempba::mp::create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation) {
    return mt::create_load_balancer(std::move(p_your_implementation));
}

gempba::load_balancer *gempba::mp::create_load_balancer(const balancing_policy &p_policy, scheduler::worker *const p_scheduler_worker) {
    if (g_load_balancer != nullptr) {
        throw std::runtime_error("load_balancer already exists!");
    }
    switch (p_policy) {
        case QUASI_HORIZONTAL: {
            g_load_balancer = std::unique_ptr<load_balancer>(new quasi_horizontal_load_balancer(p_scheduler_worker));
            break;
        }
        case WORK_STEALING: {
            g_load_balancer = std::unique_ptr<load_balancer>(new work_stealing_load_balancer(p_scheduler_worker));
            break;
        }
    }
    return g_load_balancer.get();
}

std::unique_ptr<gempba::default_mpi_stats_visitor> gempba::mp::get_default_mpi_stats_visitor() {
    return std::make_unique<default_mpi_stats_visitor>();
}

gempba::branch_handler &gempba::mp::create_branch_handler(load_balancer *p_load_balancer, scheduler::worker *p_worker) {
    if (g_branch_handler != nullptr) {
        throw std::runtime_error("branch_handler already exist!");
    }
    g_branch_handler = std::make_unique<branch_handler>(p_load_balancer, p_worker);
    return *g_branch_handler;
}

gempba::scheduler *gempba::get_scheduler() {
    if (!g_scheduler) {
        throw std::runtime_error("scheduler not yet instantiated!");
    }
    return g_scheduler.get();
}

void gempba::reset_scheduler() {
    g_scheduler.reset();
}

gempba::load_balancer *gempba::get_load_balancer() {
    return g_load_balancer.get();
}

void gempba::reset_load_balancer() {
    g_load_balancer.reset();
}

gempba::branch_handler &gempba::get_branch_handler() {
    if (!g_branch_handler) {
        // Temporarily disabled while I convert all the examples to new development
        throw std::runtime_error("scheduler not yet instantiated!");
    }
    return *g_branch_handler;
}

void gempba::reset_branch_handler() {
    g_branch_handler.reset();
}
