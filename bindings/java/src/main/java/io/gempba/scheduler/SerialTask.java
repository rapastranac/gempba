package io.gempba.scheduler;

import io.gempba.core.NodeManager;

/**
 * Task body for an MPI {@link SerialRunnable}.
 *
 * <p>Executed on a worker process when the MPI center dispatches serialised
 * arguments.  The body typically creates child nodes (via
 * {@link io.gempba.GemPBA#createExplicitNode} or {@link io.gempba.GemPBA#createExplicitNode})
 * and submits them to the local {@link NodeManager} obtained from
 * {@link io.gempba.GemPBA#getNodeManager()}.
 *
 * <h2>Return value</h2>
 * <ul>
 *   <li>For {@link io.gempba.GemPBA#runnableReturnNone} runnables — return {@code null};
 *       results are reported via {@link NodeManager#tryUpdateResult}.</li>
 *   <li>For {@link io.gempba.GemPBA#runnableReturnValue} runnables — return the
 *       serialised result bytes that should be forwarded back to the center.</li>
 * </ul>
 */
@FunctionalInterface
public interface SerialTask {

    /**
     * Executes the task.
     *
     * @param threadId  hash of the executing thread's {@code std::thread::id}
     * @param argsBytes serialised arguments sent by the center
     * @return serialised result bytes, or {@code null} for void runnables
     */
    byte[] execute(long threadId, byte[] argsBytes);
}
