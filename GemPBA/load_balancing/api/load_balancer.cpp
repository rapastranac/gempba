#include "load_balancer.hpp"
#include "load_balancing/factory/load_balancer_factory.h"

/**
 * @author Andres Pastrana
 * @date 2024-08-24
 */
namespace gempba {
    LoadBalancer &LoadBalancer::getInstance() {
        return *LoadBalancerFactory::getInstance()->getLoadBalancerInstance();
    }
}