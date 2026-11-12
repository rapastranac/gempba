package io.gempba;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

class StopNativeTest {

    @BeforeEach
    void setUp() {
        GemPBA.disableTelemetry();
    }

    @AfterEach
    void tearDown() {
        GemPBA.shutdown();
        GemPBA.enableTelemetry();
    }

    @Test
    void no_stop_is_requested_at_first() {
        assertFalse(GemPBA.isStopRequested());
    }

    @Test
    void request_stop_is_seen_until_shutdown() {
        GemPBA.requestStop();
        assertTrue(GemPBA.isStopRequested());

        GemPBA.shutdown();
        assertFalse(GemPBA.isStopRequested());
    }
}
