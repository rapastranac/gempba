package io.gempba.value;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Pure-Java tests for {@link BalancingPolicy}.
 * <p>
 * Verifies that ordinals match the C++ {@code gempba::balancing_policy} enum so
 * they can be passed through JNI without a translation table.
 * C++ enum: QUASI_HORIZONTAL=0, WORK_STEALING=1
 */
class BalancingPolicyTest {

    @Test
    void balancing_policy_ordinals_match_cpp_enum() {
        assertEquals(0, BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        assertEquals(1, BalancingPolicy.WORK_STEALING.ordinal());
    }
}
