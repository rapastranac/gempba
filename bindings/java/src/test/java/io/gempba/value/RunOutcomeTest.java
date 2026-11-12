package io.gempba.value;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class RunOutcomeTest {

    @Test
    void ordinals_match_the_cpp_enums() {
        assertEquals(0, EndReason.COMPLETED.ordinal());
        assertEquals(1, EndReason.INTERRUPTED.ordinal());
        assertEquals(0, StopTrigger.NONE.ordinal());
        assertEquals(1, StopTrigger.CODE.ordinal());
        assertEquals(2, StopTrigger.SIGNAL.ordinal());
        assertEquals(3, StopTrigger.CONTROL.ordinal());
    }

    @Test
    void from_ordinals_rebuilds_an_interrupted_run() {
        RunOutcome outcome = RunOutcome.fromOrdinals(new int[]{1, 2, 15});

        assertEquals(new RunOutcome(EndReason.INTERRUPTED, StopTrigger.SIGNAL, 15), outcome);
        assertTrue(outcome.isInterrupted());
    }

    @Test
    void from_ordinals_rebuilds_a_completed_run() {
        RunOutcome outcome = RunOutcome.fromOrdinals(new int[]{0, 0, 0});

        assertEquals(new RunOutcome(EndReason.COMPLETED, StopTrigger.NONE, 0), outcome);
        assertFalse(outcome.isInterrupted());
    }
}
