#ifndef GEMPBA_LOAD_BALANCER_FACTORY_HPP
#define GEMPBA_LOAD_BALANCER_FACTORY_HPP

#include <spdlog/spdlog.h>

#include "load_balancing/api/load_balancer.hpp"
#include "load_balancing/impl/quasi_horizontal_load_balancer.hpp"
#include "utils/utils.hpp"

/**
 * @author Andres Pastrana
 * @date 2024-08-31
 */
namespace gempba {

    class LoadBalancerFactory {
        LoadBalancer *loadBalancerInstance;

        LoadBalancerFactory() = default;

    public:

        LoadBalancerFactory(const LoadBalancerFactory &) = delete;

        void operator=(const LoadBalancerFactory &) = delete;

        ~LoadBalancerFactory() {
            delete loadBalancerInstance;
        }

        static LoadBalancerFactory *getInstance() {
            static auto *instance = new LoadBalancerFactory();
            return instance;
        }

        LoadBalancer *getLoadBalancerInstance() {
            if (loadBalancerInstance == nullptr) {
                spdlog::throw_spdlog_ex("LoadBalancer instance not created yet");
            }
            return loadBalancerInstance;
        }


        LoadBalancer &createLoadBalancer(LoadBalancingStrategy strategy) noexcept(false) {
            if (loadBalancerInstance != nullptr) {
                spdlog::throw_spdlog_ex("LoadBalancer instance already created");
            }

            switch (strategy) {
                case QUASI_HORIZONTAL: {
                    loadBalancerInstance = QuasiHorizontalLoadBalancer::getInstance();
                    return *loadBalancerInstance;
                }
                default: {
                    spdlog::throw_spdlog_ex("Invalid strategy");
                }
            }
            throw std::runtime_error("Invalid strategy");
        }
    };
}

#endif //GEMPBA_LOAD_BALANCER_FACTORY_HPP
