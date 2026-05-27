package io.gempba;

import io.gempba.core.LoadBalancer;
import io.gempba.core.Node;
import io.gempba.core.NodeManager;
import io.gempba.task.Deserializer;
import io.gempba.task.Serializer;
import io.gempba.task.VoidNodeTask;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.util.function.BiConsumer;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Integration tests for {@link NodeManager}.
 * <p>
 * Each test creates its own {@link LoadBalancer} and {@link NodeManager} and
 * tears down via {@link io.gempba.GemPBA#shutdown()} in {@code @AfterEach} so the gempba
 * singleton is reset between tests.
 */
class NodeManagerTest {

    private static final Serializer<Integer> SER = v -> ByteBuffer.allocate(4).putInt(v).array();
    private static final Deserializer<Integer> DESER = b -> ByteBuffer.wrap(b).getInt();

    @AfterEach
    void tearDown() {
        GemPBA.shutdown();
    }

    // ─── Goal: MAXIMISE ───────────────────────────────────────────────────────

    @Test
    void maximise_finds_correct_best_score() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(2);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        @SuppressWarnings("unchecked")
        VoidNodeTask<Integer>[] task = new VoidNodeTask[1];
        task[0] = (tid, value, self) -> {
            nm.tryUpdateResult(value, Score.make(value));
            if (value > 1) {
                // Mirror the C++ idiom (e.g. examples/include/mt_benchmark.hpp:75): when a node is
                // executed via the delegated path, the runtime passes self == null. Substitute a
                // dummy parent so children still get a valid root.
                Node parent = (self == null) ? GemPBA.createDummyNode(lb) : self;
                nm.tryLocalSubmit(GemPBA.createExplicitNode(lb, parent, task[0], value - 1, SER, DESER));
                nm.forward(GemPBA.createExplicitNode(lb, parent, task[0], value - 2, SER, DESER));
            }
        };

        Node seed = GemPBA.createSeedNode(lb, task[0], 5, SER, DESER);
        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        Score best = nm.getScore();
        assertEquals(ScoreType.I32, best.type);
        assertEquals(5L, best.rawBits());
    }

    // ─── Goal: MINIMISE ───────────────────────────────────────────────────────

    @Test
    void minimise_finds_correct_best_score() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MINIMISE, ScoreType.I32);
        nm.setThreadPoolSize(2);
        nm.setScore(Score.make(Integer.MAX_VALUE));

        @SuppressWarnings("unchecked")
        VoidNodeTask<Integer>[] task = new VoidNodeTask[1];
        task[0] = (tid, value, self) -> {
            Node parent = (self != null) ? self : GemPBA.createDummyNode(lb);
            nm.tryUpdateResult(value, Score.make(value));
            if (value > 1) {
                nm.tryLocalSubmit(GemPBA.createExplicitNode(lb, parent, task[0], value - 1, SER, DESER));
                nm.forward(GemPBA.createExplicitNode(lb, parent, task[0], value - 2, SER, DESER));
            }
        };

        Node seed = GemPBA.createSeedNode(lb, task[0], 5, SER, DESER);
        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        Score best = nm.getScore();
        assertEquals(ScoreType.I32, best.type);
        // minimum reachable value in the tree is 0 (5->3->1->0 via -2 branch twice)
        assertEquals(0L, best.rawBits());
    }

    // ─── isDone() ─────────────────────────────────────────────────────────────

    @Test
    void is_done_returns_true_after_completion() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        VoidNodeTask<Integer> task = (tid, value, self) -> nm.tryUpdateResult(value, Score.make(value));

        Node seed = GemPBA.createSeedNode(lb, task, 1, SER, DESER);
        nm.tryLocalSubmit(seed);
        nm.waitForCompletion();

        assertTrue(nm.isDone());
    }

    // ─── tryUpdateResult() boolean return ────────────────────────────────────

    @Test
    void try_update_result_returns_false_when_score_does_not_improve() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        // Seed the best score at MAX so nothing can beat it.
        nm.setScore(Score.make(Integer.MAX_VALUE));

        boolean[] accepted = {true};
        VoidNodeTask<Integer> task = (tid, value, self) ->
                accepted[0] = nm.tryUpdateResult(value, Score.make(value)); // value < MAX_VALUE

        Node seed = GemPBA.createSeedNode(lb, task, 42, SER, DESER);
        nm.tryLocalSubmit(seed);
        nm.waitForCompletion();

        assertFalse(accepted[0], "tryUpdateResult should return false when score does not improve");
    }

    // ─── getResultBytes() ─────────────────────────────────────────────────────

    @Test
    void result_bytes_are_stored_and_retrieved() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(2);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        @SuppressWarnings("unchecked")
        VoidNodeTask<Integer>[] task = new VoidNodeTask[1];
        task[0] = (tid, value, self) -> {
            nm.tryUpdateResult(SER.serialize(value), Score.make(value));
            if (value > 1) {
                // Mirror the C++ idiom (e.g. examples/include/mt_benchmark.hpp:75): when a node is
                // executed via the delegated path, the runtime passes self == null. Substitute a
                // dummy parent so children still get a valid root.
                Node parent = (self == null) ? GemPBA.createDummyNode(lb) : self;
                nm.tryLocalSubmit(GemPBA.createExplicitNode(lb, parent, task[0], value - 1, SER, DESER));
                nm.forward(GemPBA.createExplicitNode(lb, parent, task[0], value - 2, SER, DESER));
            }
        };

        Node seed = GemPBA.createSeedNode(lb, task[0], 5, SER, DESER);
        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        byte[] resultBytes = nm.getResultBytes();
        assertNotNull(resultBytes, "result bytes should be present");
        assertEquals(5, DESER.deserialize(resultBytes));
        assertEquals(5L, nm.getScore().rawBits());
    }

    @Test
    void get_result_bytes_null_when_no_bytes_stored() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        // MT mode: Java object stored, no byte payload → getResultBytes() is null.
        VoidNodeTask<Integer> task = (tid, value, self) -> nm.tryUpdateResult(value, Score.make(value));

        Node seed = GemPBA.createSeedNode(lb, task, 5, SER, DESER);
        nm.tryLocalSubmit(seed);
        nm.waitForCompletion();

        assertNull(nm.getResultBytes(), "getResultBytes should be null when result was stored via MT path");
    }

    // ─── getResult() — MT mode ────────────────────────────────────────────────

    @Test
    void get_result_returns_stored_object() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        VoidNodeTask<Integer> task = (tid, value, self) -> nm.tryUpdateResult(value, Score.make(value));

        Node seed = GemPBA.createSeedNode(lb, task, 7, SER, DESER);
        nm.tryLocalSubmit(seed);
        nm.waitForCompletion();

        assertEquals(7, nm.<Integer>getResult().orElseThrow());
    }

    // ─── ScoreType: I64 ───────────────────────────────────────────────────────

    @Test
    void score_type_i64_works() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I64);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Long.MIN_VALUE));

        long expectedBest = 1_000_000_000L;

        VoidNodeTask<Integer> task = (tid, value, self) -> {
            long score = (long) value * 1_000_000_000L;
            nm.tryUpdateResult(score, Score.make(score));
        };

        Node seed = GemPBA.createSeedNode(lb, task, 1, SER, DESER);
        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        Score best = nm.getScore();
        assertEquals(ScoreType.I64, best.type);
        assertEquals(expectedBest, best.rawBits());
    }

    // ─── Closure-mode nodes ───────────────────────────────────────────────────

    @Test
    void closure_task_traverses_tree_and_finds_max() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(2);
        nm.setScore(Score.make(Integer.MIN_VALUE));

        @SuppressWarnings("unchecked")
        BiConsumer<Integer, Node>[] explore = new BiConsumer[1];
        explore[0] = (depth, self) -> {
            Node parent = (self != null) ? self : GemPBA.createDummyNode(lb);
            nm.tryUpdateResult(depth, Score.make(depth));
            if (depth < 3) {
                Node left = GemPBA.createExplicitNode(lb, parent,
                        (tid, s) -> explore[0].accept(depth + 1, s));
                Node right = GemPBA.createExplicitNode(lb, parent,
                        (tid, s) -> explore[0].accept(depth + 1, s));
                nm.tryLocalSubmit(left);
                nm.forward(right);
            }
        };

        Node dummy = GemPBA.createDummyNode(lb);
        Node seed = GemPBA.createExplicitNode(lb, dummy,
                (tid, self) -> explore[0].accept(0, self));

        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        assertEquals(3L, nm.getScore().rawBits());
    }

    // ─── ScoreType: F64 ───────────────────────────────────────────────────────

    @Test
    void score_type_f64_works() throws InterruptedException {
        LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
        NodeManager nm = GemPBA.createNodeManager(lb);

        nm.setGoal(Goal.MAXIMISE, ScoreType.F64);
        nm.setThreadPoolSize(1);
        nm.setScore(Score.make(Double.NEGATIVE_INFINITY));

        double expectedBest = 3.14;

        VoidNodeTask<Integer> task = (tid, value, self) ->
                nm.tryUpdateResult(expectedBest, Score.make(expectedBest));

        Node seed = GemPBA.createSeedNode(lb, task, 1, SER, DESER);
        assertTrue(nm.tryLocalSubmit(seed));
        nm.waitForCompletion();

        Score best = nm.getScore();
        assertEquals(ScoreType.F64, best.type);
        assertEquals(Double.doubleToRawLongBits(expectedBest), best.rawBits());
    }
}
