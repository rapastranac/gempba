package io.gempba;

import io.gempba.core.LoadBalancer;
import io.gempba.core.Node;
import io.gempba.task.Serializer;
import io.gempba.value.BalancingPolicy;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.lang.ref.WeakReference;

import static org.junit.jupiter.api.Assertions.assertFalse;

class ParentReachabilityTest {

    private static final int ITERATIONS = 20_000;
    private static final int GC_ATTEMPTS = 3;
    private static final int GC_EVERY = 100;

    private static WeakReference<Node> sParent;
    private static boolean sParentCollected;
    private static int sCalls;

    @AfterEach
    void tearDown() {
        GemPBA.shutdown();
    }

    @Test
    void parent_stays_reachable_until_the_child_is_created() {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        Serializer<Integer> collectParent = v -> {
            if (++sCalls % GC_EVERY == 0) {
                for (int i = 0; i < GC_ATTEMPTS && !sParentCollected; i++) {
                    System.gc();
                    sParentCollected = sParent.get() == null;
                }
            }
            return new byte[]{v.byteValue()};
        };

        for (int i = 0; i < ITERATIONS && !sParentCollected; i++) {
            createChildOfFreshDummy(lb, collectParent).close();
        }

        assertFalse(sParentCollected, "the parent Node became unreachable while its child was being created");
    }

    private static Node createChildOfFreshDummy(LoadBalancer lb, Serializer<Integer> serializer) {
        Node dummy = GemPBA.createDummyNode(lb);
        sParent = new WeakReference<>(dummy);
        return GemPBA.createExplicitNode(lb, dummy, (tid, a, self) -> null, 0, serializer, b -> 0);
    }
}
