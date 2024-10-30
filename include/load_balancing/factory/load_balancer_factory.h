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
