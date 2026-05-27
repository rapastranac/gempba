package io.gempba.value;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;

/**
 * Pure-Java tests for {@link Goal}.
 * <p>
 * Verifies that ordinals match the C++ {@code gempba::goal} enum so they can
 * be passed through JNI without a translation table.
 * C++ enum: MAXIMISE=0, MINIMISE=1
 */
class GoalTest {

    @Test
    void goal_ordinals_match_cpp_enum() {
        assertEquals(0, Goal.MAXIMISE.ordinal());
        assertEquals(1, Goal.MINIMISE.ordinal());
    }
}
