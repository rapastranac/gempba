package io.gempba.core;

import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;

import static org.junit.jupiter.api.Assertions.*;

/**
 * JNI-level tests for {@code gempba::mt::create_explicit_node} and
 * {@code gempba::mt::create_lazy_node} bindings.
 *
 * <p>Uses {@link GemPBANative} directly without depending on {@link GemPBA}.
 */
class MTNodeCreationNativeTest {

    private static final byte[] INT_42 = ByteBuffer.allocate(4).putInt(42).array();

    @AfterEach
    void tearDown() {
        GemPBANative.shutdown();
    }

    @Test
    void createExplicitNode_returns_valid_handle() throws InterruptedException {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        long nmHandle = GemPBANative.MT.createNodeManager(lbHandle);
        NodeManager nm = new NodeManager(nmHandle);
        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        Node dummy = new Node(lbHandle, (tid, args, h) -> null);
        dummy.state.handle = GemPBANative.createDummyNode(lbHandle);

        Node child = new Node(lbHandle, (tid, args, selfHandle) -> {
            nm.tryUpdateResult(42, Score.make(42));
            return null;
        });
        child.state.handle = GemPBANative.MT.createExplicitNode(lbHandle, dummy.state.handle, child, null);
        assertNotEquals(0L, child.state.handle, "createExplicitNode must return a non-zero handle");

        assertTrue(nm.tryLocalSubmit(child));
        nm.waitForCompletion();

        assertEquals(42L, nm.getScore().rawBits());

        dummy.close();
        child.close();
    }

    @Test
    void createLazyNode_executes_when_gate_passes() throws InterruptedException {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        NodeManager nm = new NodeManager(GemPBANative.MT.createNodeManager(lbHandle));
        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        Node dummy = new Node(lbHandle, (tid, args, h) -> null);
        dummy.state.handle = GemPBANative.createDummyNode(lbHandle);

        // Gate returns non-null bytes → node executes
        Node lazy = new Node(lbHandle,
                (tid, args, selfHandle) -> {
                    nm.tryUpdateResult(7, Score.make(7));
                    return null;
                },
                () -> Node.EMPTY_BYTES);   // non-null → proceed
        lazy.state.handle = GemPBANative.MT.createLazyNode(lbHandle, dummy.state.handle, lazy, null);
        assertNotEquals(0L, lazy.state.handle);

        assertTrue(nm.tryLocalSubmit(lazy));
        nm.waitForCompletion();
        assertEquals(7L, nm.getScore().rawBits());

        dummy.close();
        lazy.close();
    }

    @Test
    void createLazyNode_pruned_when_gate_returns_null() throws InterruptedException {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        NodeManager nm = new NodeManager(GemPBANative.MT.createNodeManager(lbHandle));
        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(0));   // sentinel: the pruned task would set 99 if it ran

        Node dummy = new Node(lbHandle, (tid, args, h) -> null);
        dummy.state.handle = GemPBANative.createDummyNode(lbHandle);

        // Gate returns null → node is pruned, score must stay at 0
        Node lazy = new Node(lbHandle,
                (tid, args, selfHandle) -> {
                    nm.tryUpdateResult(99, Score.make(99));
                    return null;
                },
                () -> null);   // null → prune
        lazy.state.handle = GemPBANative.MT.createLazyNode(lbHandle, dummy.state.handle, lazy, null);

        assertTrue(nm.tryLocalSubmit(lazy));
        nm.waitForCompletion();
        assertEquals(Score.make(0).rawBits(), nm.getScore().rawBits(),
                "pruned node must not update the score");

        dummy.close();
        lazy.close();
    }
}
