#include <cstdint>
#include <gempba/gempba.hpp>
#include <gempba/node_manager.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>
#include <impl/install_scheduler.hpp>
#include <impl/load_balancing/quasi_horizontal_load_balancer.hpp>
#include <impl/load_balancing/work_stealing_load_balancer.hpp>
#include <impl/telemetry/telemetry_transport.hpp>
#include <memory>
#include <spdlog/spdlog.h>
#include <utility>
#include <vector>

#if GEMPBA_MULTIPROCESSING
    #include <impl/schedulers/mpi_centralized_scheduler.hpp>
    #include <impl/schedulers/mpi_semi_centralized_scheduler.hpp>
    #include <impl/telemetry/mpi_transport.hpp>
#endif

namespace {
    inline std::unique_ptr<gempba::scheduler> g_scheduler;
    inline std::unique_ptr<gempba::load_balancer> g_load_balancer;
    inline std::unique_ptr<gempba::node_manager> g_node_manager;
    inline gempba::detail::telemetry_transport_factory g_make_transport;

    // Install the telemetry hub on first hit of a create_* entry point.
    // Called from both mp::create_scheduler (every MPI rank reaches it -> MPI_Comm_dup
    // is collective-symmetric) and mt::create_node_manager (single-process MT_ONLY).
    void install_telemetry_hub_if_needed() {
        if (!gempba::telemetry::is_enabled()) {
            return;
        }
        if (gempba::telemetry::get() != nullptr) {
            return;
        }
        auto v_hub = std::make_unique<gempba::telemetry::telemetry_hub>();
        if (g_scheduler) {
            v_hub->describe_scheduler(g_scheduler->get_name(), g_scheduler->role_of(g_scheduler->rank_me()));
        }
        if (g_load_balancer) {
            v_hub->describe_load_balancer(g_load_balancer->get_balancing_policy());
        }
        if (g_scheduler) {
            v_hub->on_runtime_ready(gempba::telemetry::runtime_mode::MP_MPI, static_cast<std::uint32_t>(g_scheduler->rank_me()), static_cast<std::uint32_t>(g_scheduler->world_size()),
                                    g_make_transport());
        } else {
            v_hub->on_runtime_ready(gempba::telemetry::runtime_mode::MT_ONLY, 0, 1);
        }
        gempba::telemetry::install(std::move(v_hub));
    }

    void describe_load_balancer_to_telemetry() {
        if (auto* v_telemetry = gempba::telemetry::get()) {
            v_telemetry->describe_load_balancer(g_load_balancer->get_balancing_policy());
        }
    }

    gempba::load_balancer* assign_load_balancer(std::unique_ptr<gempba::load_balancer> p_implementation) {
        if (g_load_balancer != nullptr) {
            throw std::runtime_error("load_balancer already exists!");
        }
        g_load_balancer = std::move(p_implementation);
        describe_load_balancer_to_telemetry();
        return g_load_balancer.get();
    }

    gempba::load_balancer* build_load_balancer(const gempba::balancing_policy& p_policy, gempba::scheduler* const p_scheduler) {
        if (g_load_balancer != nullptr) {
            throw std::runtime_error("load_balancer already exists!");
        }
        switch (p_policy) {
            case gempba::QUASI_HORIZONTAL: {
                g_load_balancer = std::unique_ptr<gempba::load_balancer>(new gempba::quasi_horizontal_load_balancer(p_scheduler));
                break;
            }
            case gempba::WORK_STEALING: {
                g_load_balancer = std::unique_ptr<gempba::load_balancer>(new gempba::work_stealing_load_balancer(p_scheduler));
                break;
            }
        }
        describe_load_balancer_to_telemetry();
        return g_load_balancer.get();
    }

    gempba::node_manager& build_node_manager(gempba::load_balancer* const p_load_balancer, gempba::scheduler* const p_scheduler) {
        if (g_node_manager != nullptr) {
            throw std::runtime_error("node_manager already exist!");
        }
        g_node_manager = std::make_unique<gempba::node_manager>(p_load_balancer, p_scheduler);
        install_telemetry_hub_if_needed();
        return *g_node_manager;
    }
} // namespace

#if GEMPBA_DEBUG_COMMENTS
namespace {
    struct debug_logger_initializer {
        debug_logger_initializer() noexcept {
            try {
                spdlog::set_level(spdlog::level::debug);
                spdlog::info("GEMPBA_DEBUG_COMMENTS enabled");
            } catch (...) {
                // static-init context; no logger to fall back to
            }
        }
    };

    // create one instance that runs at static initialization time
    inline debug_logger_initializer g_debug_logger_initializer_instance;
} // namespace
#endif

void gempba::check_not_null([[maybe_unused]] const node& p_parent) {
    if (p_parent == nullptr) {
        utils::log_and_throw("Node creation cannot have a nullptr for a parent");
    }
}

gempba::load_balancer* gempba::create_load_balancer(std::unique_ptr<load_balancer> p_your_implementation) { return assign_load_balancer(std::move(p_your_implementation)); }

gempba::scheduler* gempba::detail::install_scheduler(std::unique_ptr<scheduler> p_scheduler, telemetry_transport_factory p_make_transport) {
    if (g_scheduler != nullptr) {
        throw std::runtime_error("scheduler already exists!");
    }
    g_scheduler = std::move(p_scheduler);
    g_make_transport = std::move(p_make_transport);
    install_telemetry_hub_if_needed();
    return g_scheduler.get();
}

#if !GEMPBA_MULTIPROCESSING || GEMPBA_DEV_MODE

gempba::load_balancer* gempba::multithreading::create_load_balancer(const balancing_policy& p_policy) { return build_load_balancer(p_policy, nullptr); }

gempba::node_manager& gempba::multithreading::create_node_manager(load_balancer* p_load_balancer) { return build_node_manager(p_load_balancer, nullptr); }

#endif // !GEMPBA_MULTIPROCESSING || GEMPBA_DEV_MODE

#if GEMPBA_MULTIPROCESSING

namespace {
    std::unique_ptr<gempba::telemetry::telemetry_transport> make_mpi_transport() { return std::make_unique<gempba::telemetry::mpi_transport>(); }
} // namespace

gempba::scheduler* gempba::multiprocessing::create_scheduler(std::unique_ptr<scheduler> p_your_implementation) {
    return detail::install_scheduler(std::move(p_your_implementation), make_mpi_transport);
}

gempba::scheduler* gempba::multiprocessing::create_scheduler(const scheduler_topology& p_topology) {
    if (g_scheduler != nullptr) {
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

gempba::load_balancer* gempba::multiprocessing::create_load_balancer(const balancing_policy& p_policy, scheduler* const p_scheduler) { return build_load_balancer(p_policy, p_scheduler); }

std::unique_ptr<gempba::default_mpi_stats_visitor> gempba::multiprocessing::get_default_mpi_stats_visitor() { return std::make_unique<default_mpi_stats_visitor>(); }

gempba::node_manager& gempba::multiprocessing::create_node_manager(load_balancer* p_load_balancer, scheduler* p_scheduler) { return build_node_manager(p_load_balancer, p_scheduler); }

#endif // GEMPBA_MULTIPROCESSING

gempba::scheduler* gempba::get_scheduler() {
    if (!g_scheduler) {
        throw std::runtime_error("scheduler not yet instantiated!");
    }
    return g_scheduler.get();
}

gempba::scheduler* gempba::try_get_scheduler() noexcept { return g_scheduler.get(); }

void gempba::reset_scheduler() {
    g_scheduler.reset();
    g_make_transport = nullptr;
}

gempba::load_balancer* gempba::get_load_balancer() { return g_load_balancer.get(); }

void gempba::reset_load_balancer() { g_load_balancer.reset(); }

gempba::node_manager& gempba::get_node_manager() {
    if (!g_node_manager) {
        // Temporarily disabled while I convert all the examples to new development
        throw std::runtime_error("scheduler not yet instantiated!");
    }
    return *g_node_manager;
}

void gempba::reset_node_manager() { g_node_manager.reset(); }

int gempba::shutdown() {
    telemetry::uninstall();
    g_scheduler.reset();
    g_make_transport = nullptr;
    g_load_balancer.reset();
    g_node_manager.reset();
    return EXIT_SUCCESS;
}
