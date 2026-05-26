package io.gempba.scheduler;

import io.gempba.core.LoadBalancer;
import io.gempba.core.NodeManager;
import io.gempba.stats.RankStats;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;

import java.util.*;

/**
 * Java handle to a C++ {@code gempba::scheduler} instance.
 *
 * <p>Controls MPI-level coordination between processes.  Instances are
 * obtained from {@link io.gempba.GemPBA#createScheduler}.
 *
 * <p>The typical MPI pattern is:
 * <pre>{@code
 * Scheduler sched = GemPBA.createScheduler(SchedulerTopology.SEMI_CENTRALIZED, 3.0);
 * sched.setGoal(Goal.MAXIMISE, ScoreType.I32);
 *
 * if (sched.rankMe() == 0) {
 *     sched.center().run(initialTaskBytes, RUNNABLE_ID);
 *     byte[] result = sched.center().getResult();
 * } else {
 *     LoadBalancer lb  = GemPBA.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL);
 *     NodeManager  nm  = GemPBA.createNodeManager(lb);
 *     nm.setThreadPoolSize(4);
 *     nm.setScore(Score.make(Integer.MIN_VALUE));
 *
 *     Map<Integer, SerialRunnable> runnables = Map.of(
 *         RUNNABLE_ID, GemPBA.runnableReturnNone(RUNNABLE_ID, (tid, bytes) -> explore(tid, bytes)));
 *     sched.worker().run(nm, runnables);
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

    /**
     * Returns the coordinator view.  Call only from rank 0.
     *
     * <p>Equivalent to {@code scheduler.center_view()} in C++.
     */
    public Center center() {
        return new Center(GemPBANative.Scheduler.getCenterHandle(handle));
    }

    /**
     * Returns the worker view.  Call from all non-zero ranks.
     *
     * <p>Equivalent to {@code scheduler.worker_view()} in C++.
     */
    public Worker worker() {
        return new Worker(GemPBANative.Scheduler.getWorkerHandle(handle));
    }

    // ─── Center ───────────────────────────────────────────────────────────────

    /**
     * MPI coordinator view of the scheduler.
     *
     * <p>Mirrors {@code gempba::scheduler::center} in C++.
     */
    public static final class Center {

        private final long handle;

        Center(long handle) {
            this.handle = handle;
        }

        /**
         * MPI barrier — see {@link Scheduler#barrier()}.
         */
        public void barrier() {
            GemPBANative.Scheduler.Center.barrier(handle);
        }

        /**
         * Rank of this process — see {@link Scheduler#rankMe()}.
         */
        public int rankMe() {
            return GemPBANative.Scheduler.Center.rankMe(handle);
        }

        /**
         * World size — see {@link Scheduler#worldSize()}.
         */
        public int worldSize() {
            return GemPBANative.Scheduler.Center.worldSize(handle);
        }

        /**
         * Starts the coordinator loop, dispatching the initial task to a worker
         * and orchestrating the distributed search until completion.
         *
         * <p>Equivalent to {@code center.run(task, runnable_id)} in C++.
         * Blocks until the distributed search finishes.
         *
         * @param task       serialised initial arguments for the root task
         * @param runnableId ID of the {@link SerialRunnable} registered on workers
         */
        public void run(byte[] task, int runnableId) {
            GemPBANative.Scheduler.Center.run(handle, task, runnableId);
        }

        /**
         * Returns the best result found across all worker processes.
         *
         * <p>Equivalent to {@code center.get_result()} in C++.
         * Call after {@link #run} returns.
         *
         * @return serialised best result bytes, or {@code null} if no result was found
         */
        public byte[] getResult() {
            return GemPBANative.Scheduler.Center.getResult(handle);
        }

        /**
         * Returns all results from all worker processes.
         *
         * <p>Equivalent to {@code center.get_all_results()} in C++.
         * The element at index {@code i} corresponds to rank {@code i};
         * rank 0 (the center itself) is always {@code null}.
         *
         * @return list of per-rank result bytes; elements may be {@code null}
         */
        public List<byte[]> getAllResults() {
            byte[][] raw = GemPBANative.Scheduler.Center.getAllResults(handle);
            return (raw == null) ? Collections.emptyList() : Arrays.asList(raw);
        }
    }

    // ─── Worker ───────────────────────────────────────────────────────────────

    /**
     * MPI worker view of the scheduler.
     *
     * <p>Mirrors {@code gempba::scheduler::worker} in C++.
     */
    public static final class Worker {

        /**
         * Handle to the C++ {@code gempba::scheduler::worker*}.
         */
        public final long handle;

        Worker(long handle) {
            this.handle = handle;
        }

        /**
         * MPI barrier — see {@link Scheduler#barrier()}.
         */
        public void barrier() {
            GemPBANative.Scheduler.Worker.barrier(handle);
        }

        /**
         * Rank of this process — see {@link Scheduler#rankMe()}.
         */
        public int rankMe() {
            return GemPBANative.Scheduler.Worker.rankMe(handle);
        }

        /**
         * World size — see {@link Scheduler#worldSize()}.
         */
        public int worldSize() {
            return GemPBANative.Scheduler.Worker.worldSize(handle);
        }

        /**
         * Enters the MPI worker loop; blocks until the distributed search is done.
         *
         * <p>Equivalent to {@code worker.run(node_manager, runnables)} in C++.
         *
         * @param nm        the local node manager that owns the thread pool
         * @param runnables map from integer ID to the corresponding {@link SerialRunnable}
         */
        public void run(NodeManager nm, Map<Integer, SerialRunnable> runnables) {
            int n = runnables.size();
            int[] ids = new int[n];
            long[] handles = new long[n];
            int i = 0;
            for (Map.Entry<Integer, SerialRunnable> e : runnables.entrySet()) {
                ids[i] = e.getKey();
                handles[i] = e.getValue().handle;
                i++;
            }
            GemPBANative.Scheduler.Worker.run(handle, nm.handle, ids, handles);
        }

    }
}
