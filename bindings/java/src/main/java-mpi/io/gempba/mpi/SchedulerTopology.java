package io.gempba.mpi;

/**
 * MPI scheduler topology, mirroring {@code gempba::mpi::scheduler_topology}.
 *
 * <p>Passed to {@link Mpi#createScheduler(SchedulerTopology)}.
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
