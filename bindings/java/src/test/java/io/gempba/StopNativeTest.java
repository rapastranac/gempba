package io.gempba;

import io.gempba.core.LoadBalancer;
import io.gempba.internal.StopSignals;
import io.gempba.value.BalancingPolicy;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import sun.misc.Signal;

import static org.junit.jupiter.api.Assertions.*;

class StopNativeTest {

    @BeforeEach
    void setUp() {
        GemPBA.disableTelemetry();
    }

    @AfterEach
    void tearDown() {
        GemPBA.shutdown();
        GemPBA.stopOnSignals(true);
        GemPBA.enableTelemetry();
    }

    private static void createNodeManager() {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        GemPBA.createNodeManager(lb);
    }

    private static boolean waitForStop() throws InterruptedException {
        long deadline = System.nanoTime() + 2_000_000_000L;
        while (System.nanoTime() < deadline) {
            if (GemPBA.isStopRequested()) {
                return true;
            }
            Thread.sleep(10);
        }
        return GemPBA.isStopRequested();
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

    @Test
    void creating_the_node_manager_takes_both_signals_and_shutdown_gives_them_back() {
        createNodeManager();
        assertTrue(StopSignals.isTaken("INT"));
        assertTrue(StopSignals.isTaken("TERM"));

        GemPBA.shutdown();

        assertFalse(StopSignals.isTaken("INT"));
        assertFalse(StopSignals.isTaken("TERM"));
    }

    @Test
    void sigterm_stops_the_run_instead_of_the_jvm() throws InterruptedException {
        createNodeManager();
        assertTrue(StopSignals.isTaken("TERM"));

        Signal.raise(new Signal("TERM"));

        assertTrue(waitForStop());
        assertTrue(StopSignals.isTaken("TERM"));
    }

    @Test
    void turned_off_the_node_manager_takes_no_signal() {
        GemPBA.stopOnSignals(false);

        createNodeManager();

        assertFalse(StopSignals.isTaken("INT"));
        assertFalse(StopSignals.isTaken("TERM"));
    }
}
