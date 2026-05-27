package io.gempba.scheduler;

/**
 * MPI scheduler topology, mirroring {@code gempba::mp::scheduler_topology}.
 *
 * <p>Passed to {@link io.gempba.GemPBA#createScheduler(SchedulerTopology, double)}.
 */
public enum SchedulerTopology {
    /**
     * Rank 0 acts as a lightweight coordinator; workers exchange tasks peer-to-peer.
     */
    SEMI_CENTRALIZED,
    /**
     * All task routing goes through rank 0.
     */
    CENTRALIZED
}
