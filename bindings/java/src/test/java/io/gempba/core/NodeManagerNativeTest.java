package io.gempba.core;

import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * JNI-level tests for the {@code gempba::node_manager} binding.
 *
 * <p>Exercises {@link GemPBANative.NodeManager} directly without depending on
 * {@link GemPBA}. Full end-to-end tests that use the high-level API live in
 * {@link NodeManagerTest}.
 */
class NodeManagerNativeTest {

    @AfterEach
    void tearDown() {
        GemPBANative.shutdown();
    }

    @Test
    void createNodeManager_returns_valid_handle() {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        long nmHandle = GemPBANative.MT.createNodeManager(lbHandle);
        assertNotEquals(0L, nmHandle, "MT.createNodeManager must return a non-zero handle");
        GemPBANative.NodeManager.destroy(nmHandle);
    }

    @Test
    void destroy_with_zero_handle_is_safe() {
        GemPBANative.NodeManager.destroy(0L);
    }

    @Test
    void leaf_task_reports_score_via_tryUpdateResultMT() throws InterruptedException {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        NodeManager nm = new NodeManager(GemPBANative.MT.createNodeManager(lbHandle));

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        Node seed = new Node(lbHandle, (tid, args, selfHandle) -> {
            nm.tryUpdateResult(42, Score.make(42));
            return null;
        });
        seed.state.handle = GemPBANative.createSeedNode(lbHandle, seed, null);

        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        Score best = nm.getScore();
        assertEquals(ScoreType.I32, best.type);
        assertEquals(42L, best.rawBits());

        seed.close();
    }
}
