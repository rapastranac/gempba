package io.gempba.core;

import io.gempba.value.BalancingPolicy;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertNotEquals;

/**
 * Tests for the {@code gempba::load_balancer} JNI binding.
 *
 * <p>Verifies load-balancer creation and safe destruction for every
 * {@link BalancingPolicy}. Full end-to-end scheduling tests that require a live
 * node manager live in {@link NodeManagerTest}.
 */
class LoadBalancerTest {

    private long lbHandle;

    @AfterEach
    void tearDown() {
        if (lbHandle != 0L) {
            GemPBANative.LoadBalancer.destroy(lbHandle);
            lbHandle = 0L;
        }
        GemPBANative.shutdown();
    }

    @Test
    void create_quasi_horizontal_load_balancer_returns_valid_handle() {
        lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        assertNotEquals(0L, lbHandle,
                "MT.createLoadBalancer(QUASI_HORIZONTAL) must return a non-zero handle");
    }

    @Test
    void create_work_stealing_load_balancer_returns_valid_handle() {
        lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.WORK_STEALING.ordinal());
        assertNotEquals(0L, lbHandle,
                "MT.createLoadBalancer(WORK_STEALING) must return a non-zero handle");
    }

    @Test
    void destroy_with_zero_handle_is_safe() {
        GemPBANative.LoadBalancer.destroy(0L);   // must not crash
    }
}
