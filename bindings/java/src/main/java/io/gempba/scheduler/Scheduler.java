package io.gempba.scheduler;

import io.gempba.core.LoadBalancer;
import io.gempba.core.NodeManager;
import io.gempba.stats.RankStats;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.RunOutcome;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;

import java.util.*;

/**
 * Java handle to a C++ {@code gempba::scheduler} instance.
 *
 * <p>Controls the coordination between processes.  The built-in MPI ones
 * come from {@code io.gempba.mpi.Mpi#createScheduler}, in the {@code gempba-mpi} artifact.
 *
 * <p>The typical MPI pattern is:
 * <pre>{@code
 * Scheduler sched = Mpi.createScheduler(SchedulerTopology.SEMI_CENTRALIZED);
 * sched.setGoal(Goal.MAXIMISE, ScoreType.I32);
 *
 * LoadBalancer lb = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL, sched);
 * NodeManager  nm = GemPBA.createNodeManager(lb, sched);
 * nm.setThreadPoolSize(4);
 * nm.setScore(Score.make(Integer.MIN_VALUE));
 *
 * Map<Integer, SerialRunnable> runnables = Map.of(
 *     RUNNABLE_ID, GemPBA.runnableReturnNone(RUNNABLE_ID, (tid, bytes) -> explore(tid, bytes)));
 * if (sched.isRoot()) {
 *     sched.run(nm, runnables, initialTaskBytes, RUNNABLE_ID);
 *     byte[] result = sched.getResult();
 * } else {
 *     sched.run(nm, runnables);
 * }
 * GemPBA.shutdown();
 * }</pre>
 */
public final class Scheduler {

    /**
     * Handle to the C++ {@code gempba::scheduler*}.
     */
    public final long handle;

    public Scheduler(long handle) {
        if (handle == 0L)
            throw new IllegalStateException("Native scheduler creation failed (null handle)");
        this.handle = handle;
    }

    // ─── scheduler_traits ─────────────────────────────────────────────────────

    /**
     * Synchronizes all processes (MPI barrier).
     *
     * <p>Equivalent to {@code scheduler.barrier()} in C++.
     */
    public void barrier() {
        GemPBANative.Scheduler.barrier(handle);
    }

    /**
     * Returns the MPI rank of this process in the world communicator.
     *
     * <p>Equivalent to {@code scheduler.rank_me()} in C++.
     */
    public int rankMe() {
        return GemPBANative.Scheduler.rankMe(handle);
    }

    /**
     * Returns the total number of processes in the world communicator.
     *
     * <p>Equivalent to {@code scheduler.world_size()} in C++.
     */
    public int worldSize() {
        return GemPBANative.Scheduler.worldSize(handle);
    }

    // ─── scheduler ────────────────────────────────────────────────────────────

    /**
     * Sets the optimisation goal and score type for the distributed search.
     *
     * <p>Equivalent to {@code scheduler.set_goal(goal, score_type)} in C++.
     * Must be called by all processes before entering the run loop.
     */
    public void setGoal(Goal goal, ScoreType scoreType) {
        GemPBANative.Scheduler.setGoal(handle, goal.ordinal(), scoreType.cppOrdinal());
    }

    /**
     * Returns the elapsed wall-clock time in seconds since the scheduler started.
     *
     * <p>Equivalent to {@code scheduler.elapsed_time()} in C++.
     */
    public double elapsedTime() {
        return GemPBANative.Scheduler.elapsedTime(handle);
    }

    /**
     * Broadcasts and collects statistics across all processes.
     *
     * <p>Equivalent to {@code scheduler.synchronize_stats()} in C++.
     * Must be called by all processes.
     */
    public void synchronizeStats() {
        GemPBANative.Scheduler.synchronizeStats(handle);
    }

    /**
     * Returns per-rank statistics collected at rank 0 after
     * {@link #synchronizeStats()}.
     *
     * <p>Equivalent to iterating {@code scheduler.get_stats_vector()} with a
     * {@code default_mpi_stats_visitor} in C++.  Each {@link RankStats} entry is
     * a name-keyed map: new fields added to the C++ visitor surface automatically
     * without any changes to this method or to {@link RankStats}.
     *
     * <p>Call only from rank 0.
     *
     * @return one {@link RankStats} per MPI rank, ordered by rank index
     */
    public List<RankStats> getStats() {
        Object[] raw = GemPBANative.Scheduler.getStats(handle);
        String[] names = (String[]) raw[0];
        Object[][] vals = (Object[][]) raw[1];

        List<RankStats> result = new ArrayList<>(vals.length);
        for (int r = 0; r < vals.length; r++) {
            LinkedHashMap<String, Number> map = new LinkedHashMap<>();
            for (int i = 0; i < names.length; i++) {
                if (vals[r][i] instanceof Number n) {
                    map.put(names[i], n);
                }
                // Non-Number types (e.g. Boolean added in the future) are silently
                // skipped here; they will not appear in the RankStats map.
            }
            result.add(new RankStats(r, map));
        }
        return Collections.unmodifiableList(result);
    }

    // ─── Run and the root's results ───────────────────────────────────────────

    /**
     * Whether this rank is the root, rank 0: the rank that seeds the run and
     * reads its results.
     *
     * <p>Equivalent to {@code scheduler.is_root()} in C++.
     */
    public boolean isRoot() {
        return GemPBANative.Scheduler.isRoot(handle);
    }

    /**
     * Runs this rank's part of the search on a rank other than the root;
     * blocks until the whole run has ended.
     *
     * <p>Equivalent to {@code scheduler.run(node_manager, runnables)} in C++.
     *
     * @param nm        the local node manager that owns the thread pool
     * @param runnables map from integer ID to the corresponding {@link SerialRunnable}, the same on every rank
     * @return how the run ended, the same on every rank
     */
    public RunOutcome run(NodeManager nm, Map<Integer, SerialRunnable> runnables) {
        return run(nm, runnables, null, 0);
    }

    /**
     * Runs this rank's part of the search; every rank calls it, and it blocks
     * until the whole run has ended.  Only the root passes a seed.
     *
     * <p>Equivalent to {@code scheduler.run(node_manager, runnables, seed)} in C++.
     *
     * @param nm             the local node manager that owns the thread pool
     * @param runnables      map from integer ID to the corresponding {@link SerialRunnable}, the same on every rank
     * @param seed           the root's first task, serialised; {@code null} on every other rank
     * @param seedRunnableId ID of the {@link SerialRunnable} that runs the seed; ignored when {@code seed} is {@code null}
     * @return how the run ended, the same on every rank
     */
    public RunOutcome run(NodeManager nm, Map<Integer, SerialRunnable> runnables, byte[] seed, int seedRunnableId) {
        int n = runnables.size();
        int[] ids = new int[n];
        long[] handles = new long[n];
        int i = 0;
        for (Map.Entry<Integer, SerialRunnable> e : runnables.entrySet()) {
            ids[i] = e.getKey();
            handles[i] = e.getValue().handle;
            i++;
        }
        return RunOutcome.fromOrdinals(GemPBANative.Scheduler.run(handle, nm.handle, ids, handles, seed, seedRunnableId));
    }

    /**
     * Returns the best result found across all ranks.  Call on the root after
     * {@link #run} returns.
     *
     * <p>Equivalent to {@code scheduler.get_result()} in C++.
     *
     * @return serialised best result bytes, empty if no result was found
     */
    public byte[] getResult() {
        return GemPBANative.Scheduler.getResult(handle);
    }

    /**
     * Returns each rank's best result.  Call on the root after {@link #run}
     * returns.
     *
     * <p>Equivalent to {@code scheduler.get_all_results()} in C++.
     * The element at index {@code i} corresponds to rank {@code i}, and is
     * {@code null} where that rank found none, such as rank 0 under the
     * built-in schedulers, whose coordinator runs no tasks.
     *
     * @return list of per-rank result bytes; elements may be {@code null}
     */
    public List<byte[]> getAllResults() {
        byte[][] raw = GemPBANative.Scheduler.getAllResults(handle);
        return (raw == null) ? Collections.emptyList() : Arrays.asList(raw);
    }
}
