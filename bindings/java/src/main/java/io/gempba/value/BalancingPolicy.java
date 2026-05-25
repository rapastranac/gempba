package io.gempba.value;

/**
 * Load-balancing strategy, mirroring {@code gempba::balancing_policy}.
 *
 * <p>Ordinal values match the C++ enum definition exactly.
 */
public enum BalancingPolicy {
    /**
     * GemPBA's novel quasi-horizontal dynamic load balancer.
     */
    QUASI_HORIZONTAL,   // ordinal 0
    /**
     * Classic work-stealing strategy.
     */
    WORK_STEALING       // ordinal 1
}
