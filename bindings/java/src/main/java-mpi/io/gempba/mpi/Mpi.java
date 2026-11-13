package io.gempba.mpi;

import io.gempba.internal.GemPBANative;
import io.gempba.scheduler.Scheduler;

/**
 * The MPI component's calls, mirroring C++ {@code gempba::mpi}.
 *
 * <p>Only the {@code gempba-mpi} artifact contains this package; the {@code gempba} artifact's native library has no MPI.
 */
public final class Mpi {

    static {
        var ignored = GemPBANative.LOADED;
    }

    private Mpi() {
    }

    /**
     * Creates one of the built-in MPI schedulers.  Every rank calls it.
     *
     * <p>Equivalent to {@code gempba::mpi::create_scheduler(topology)} in C++.
     *
     * @param topology distributed coordination pattern
     * @return a new {@link Scheduler}
     */
    public static Scheduler createScheduler(SchedulerTopology topology) {
        return new Scheduler(createSchedulerHandle(topology.ordinal()));
    }

    private static native long createSchedulerHandle(int topologyOrdinal);
}
