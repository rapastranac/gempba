package io.gempba.scheduler;

import io.gempba.core.NodeManager;

import io.gempba.internal.GemPBANative;

/**
 * A serializable task registered on a {@link Scheduler.Worker} by integer ID.
 *
 * <p>When the MPI center dispatches work to a worker process the worker looks
 * up the runnable by ID, deserialises the incoming bytes, and executes the
 * task on a thread-pool thread.
 *
 * <p>Instances are created via {@link io.gempba.GemPBA#runnableReturnNone} or
 * {@link io.gempba.GemPBA#runnableReturnValue} and passed to
 * {@link Scheduler.Worker#run(NodeManager, java.util.Map)}.
 */
public final class SerialRunnable implements AutoCloseable {

    /**
     * Integer ID used to look up this runnable in the worker's runnables map.
     */
    public final int id;
    /**
     * The Java task body, called from the JNI shim.
     */
    private final SerialTask serialTask;
    /**
     * Pointer to the heap-allocated C++ {@code java_serial_runnable} wrapper.
     * Zero until the factory method sets it after the JNI call.
     */
    public volatile long handle;

    public SerialRunnable(int id, long handle, SerialTask serialTask) {
        this.id = id;
        this.handle = handle;
        this.serialTask = serialTask;
    }

    // ─── JNI callback ─────────────────────────────────────────────────────────

    /**
     * Called by the JNI shim when the C++ serial runnable is executed.
     * <strong>Not part of the public API — never call this directly.</strong>
     *
     * @param threadId  hash of the executing thread's {@code std::thread::id}
     * @param argsBytes deserialised argument bytes
     * @param ignored   always 0 for serial runnables (no self handle)
     * @return result bytes forwarded back to the center, or {@code null}
     */
    byte[] _execute(long threadId, byte[] argsBytes, long ignored) {
        try {
            return serialTask.execute(threadId, argsBytes);
        } catch (Throwable t) {
            System.err.println("[gempba] SerialRunnable[id=" + id + "] threw on threadId=" + threadId + ":");
            t.printStackTrace(System.err);
            System.err.flush();
            if (t instanceof RuntimeException re) throw re;
            if (t instanceof Error err) throw err;
            throw new RuntimeException(t);
        }
    }

    // ─── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Releases the underlying C++ runnable and its JNI global reference.
     *
     * <p>After this call the object must not be used again.
     */
    @Override
    public void close() {
        if (handle != 0L) {
            GemPBANative.SerialRunnable.destroy(handle);
        }
    }
}
