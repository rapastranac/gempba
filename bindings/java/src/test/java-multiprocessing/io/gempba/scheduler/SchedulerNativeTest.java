package io.gempba.scheduler;

import io.gempba.core.LoadBalancer;
import io.gempba.core.NodeManager;
import io.gempba.stats.RankStats;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.TestInstance;

import java.nio.charset.StandardCharsets;

import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * JNI-level tests for {@code gempba::scheduler} bindings.
 *
 * <p><strong>These tests require a multi-process IPC runtime.</strong>
 * The distributed protocol runs once in {@link #runDistributedSetup} and all
 * {@code @Test} methods are pure assertions on the captured state — no JNI
 * calls after setup.  This is required because MPI allows only one
 * {@code MPI_Init}/{@code MPI_Finalize} lifecycle per process.
 *
 * <p>Run with:
 * <pre>
 *   mvn test -Pdistributed                             # default: mpirun -n 2
 *   mvn test -Pdistributed -Dgempba.launcher=mpiexec   # MS-MPI on Windows
 *   mvn test -Pdistributed -Dgempba.np=4               # 1 center + 3 workers
 * </pre>
 */
@TestInstance(TestInstance.Lifecycle.PER_CLASS)
class SchedulerNativeTest {

    private static final int RUNNABLE_ID = 1;

    // ── State captured once in @BeforeAll ─────────────────────────────────────

    private long schedHandle;
    private int rank;
    private int worldSize;
    private byte[] centerResult;        // rank 0 only
    private byte[][] allResults;        // rank 0 only
    private java.util.List<RankStats> stats; // rank 0 only, captured after synchronizeStats

    // ── Setup ─────────────────────────────────────────────────────────────────

    /**
     * Runs the entire distributed round-trip once, then shuts down MPI.
     * All @Test methods below only assert on the state captured here.
     */
    @BeforeAll
    void runDistributedSetup() {
        assumeTrue(
                Boolean.getBoolean("gempba.distributed"),
                "Skipping: not in a distributed context — use the 'distributed' Maven profile"
        );

        schedHandle = GemPBANative.Scheduler.create(SchedulerTopology.SEMI_CENTRALIZED.ordinal(), 3.0);
        rank = GemPBANative.Scheduler.rankMe(schedHandle);
        worldSize = GemPBANative.Scheduler.worldSize(schedHandle);

        GemPBANative.Scheduler.setGoal(schedHandle, Goal.MAXIMISE.ordinal(), ScoreType.I32.cppOrdinal());

        if (rank == 0) {
            // ── Center ──────────────────────────────────────────────────────
            long centerHandle = GemPBANative.Scheduler.getCenterHandle(schedHandle);
            GemPBANative.Scheduler.Center.run(
                    centerHandle,
                    "Marco!".getBytes(StandardCharsets.UTF_8),
                    RUNNABLE_ID);
            centerResult = GemPBANative.Scheduler.Center.getResult(centerHandle);
            allResults = GemPBANative.Scheduler.Center.getAllResults(centerHandle);

        } else {
            // ── Worker ──────────────────────────────────────────────────────
            long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
            long nmHandle = GemPBANative.MT.createNodeManager(lbHandle);

            NodeManager nm = new NodeManager(nmHandle);
            nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
            nm.setThreadPoolSize(2);
            nm.setScore(Score.make(Integer.MIN_VALUE));

            // Void runnable: receives "Marco!", stores "Polo!" via tryUpdateResult.
            // send_final_solution_to_center reads result bytes from the node manager,
            // so the task must call tryUpdateResult explicitly — the runnable's return
            // value is not used by the semi-centralized scheduler.
            SerialRunnable sr = new SerialRunnable(RUNNABLE_ID, 0L, (tid, args) -> {
                nm.tryUpdateResult("Polo!".getBytes(StandardCharsets.UTF_8), Score.make(1));
                return null;
            });
            sr.handle = GemPBANative.SerialRunnable.create(RUNNABLE_ID, sr, false);

            long workerHandle = GemPBANative.Scheduler.getWorkerHandle(schedHandle);
            GemPBANative.Scheduler.Worker.run(
                    workerHandle, nmHandle,
                    new int[]{RUNNABLE_ID},
                    new long[]{sr.handle});
            sr.close();
        }

        GemPBANative.Scheduler.synchronizeStats(schedHandle);
        if (rank == 0) {
            Scheduler sched = new Scheduler(schedHandle);
            stats = sched.getStats();
        }

        GemPBANative.shutdown();
    }

    // ── Tests (pure assertions on captured state) ─────────────────────────────

    @Test
    void create_returns_valid_handle() {
        assertNotEquals(0L, schedHandle, "create() must return a non-zero scheduler handle");
    }

    @Test
    void rank_and_world_size_are_consistent() {
        assertTrue(rank >= 0, "rank must be non-negative");
        assertTrue(rank < worldSize, "rank must be less than world size");
        assertTrue(worldSize >= 2, "world size must be at least 2 for MP tests");
    }

    @Test
    void center_receives_polo() {
        assumeTrue(rank == 0, "center assertions only run on rank 0");
        assertNotNull(centerResult, "center must receive a result from the workers");
        assertEquals("Polo!", new String(centerResult, StandardCharsets.UTF_8));
    }

    @Test
    void get_all_results_has_one_entry_per_rank() {
        assumeTrue(rank == 0, "center assertions only run on rank 0");
        assertNotNull(allResults, "getAllResults must not return null");
        assertEquals(worldSize, allResults.length, "getAllResults must have one entry per MPI rank");
        assertNull(allResults[0], "center rank (index 0) must have null result");
    }

    // ── getStats / RankStats ──────────────────────────────────────────────────

    @Test
    void getStats_returns_one_entry_per_rank() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        assertNotNull(stats, "getStats() must not return null");
        assertEquals(worldSize, stats.size(), "getStats() must return one RankStats per MPI rank");
    }

    @Test
    void getStats_rank_indices_are_sequential() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        for (int r = 0; r < stats.size(); r++) {
            assertEquals(r, stats.get(r).rank, "RankStats at index " + r + " must have rank == " + r);
        }
    }

    @Test
    void getStats_all_values_are_Number_subtypes() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        for (RankStats rs : stats) {
            rs.forEach((name, value) -> {
                assertInstanceOf(Number.class, value, "Stat '" + name + "' at rank " + rs.rank + " must be a Number");
            });
        }
    }

    @Test
    void getStats_known_fields_are_present_for_every_rank() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        for (RankStats rs : stats) {
            assertNotNull(rs.get("elapsed_time"), "elapsed_time must be present at rank " + rs.rank);
            assertNotNull(rs.get("idle_time"), "idle_time must be present at rank " + rs.rank);
        }
    }

    @Test
    void getStats_all_default_mpi_stats_fields_are_present_with_expected_types() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        for (RankStats rs : stats) {
            String at = " at rank " + rs.rank;
            assertInstanceOf(Long.class, rs.get("received_task_count"), "received_task_count" + at);
            assertInstanceOf(Long.class, rs.get("sent_task_count"), "sent_task_count" + at);
            assertInstanceOf(Long.class, rs.get("total_requested_tasks"), "total_requested_tasks" + at);
            assertInstanceOf(Long.class, rs.get("total_thread_requests"), "total_thread_requests" + at);
            assertInstanceOf(Double.class, rs.get("idle_time"), "idle_time" + at);
            assertInstanceOf(Double.class, rs.get("elapsed_time"), "elapsed_time" + at);
        }
    }

    @Test
    void getStats_elapsed_time_is_non_negative() {
        assumeTrue(rank == 0, "stats are collected at rank 0 only");
        for (RankStats rs : stats) {
            Number elapsed = rs.get("elapsed_time");
            assertNotNull(elapsed, "elapsed_time must be present");
            assertTrue(elapsed.doubleValue() >= 0.0, "elapsed_time must be non-negative at rank " + rs.rank);
        }
    }
}
