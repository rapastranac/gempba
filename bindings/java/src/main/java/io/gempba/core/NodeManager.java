package io.gempba.core;

import io.gempba.task.Deserializer;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;

import java.lang.ref.Reference;
import java.util.Optional;

/**
 * Java handle to a C++ {@code gempba::node_manager} instance.
 *
 * <p>Manages the thread pool, tracks the best result, and coordinates node
 * submission. Instances are obtained from {@link io.gempba.GemPBA#createNodeManager}.
 *
 * <p>All methods on this class are safe to call from multiple threads
 * concurrently; the C++ implementation provides the necessary locking.
 */
public final class NodeManager implements AutoCloseable {

    /**
     * Pointer to the C++ {@code node_manager*}, cast to {@code jlong}.
     * Public so the binding's other packages (io.gempba.scheduler) can pass it
     * through to the JNI surface; not intended for direct user use.
     */
    public final long handle;

    /**
     * MT-mode result storage (mirrors the {@code std::any} arm of the C++
     * {@code m_result} variant). Written only when the C++ score check passes,
     * so the lock here serialises the check+store pair just as the C++ mutex does.
     */
    private volatile Object m_javaResult;
    private final Object m_javaResultLock = new Object();

    public NodeManager(long handle) {
        if (handle == 0L) {
            throw new IllegalStateException("Native node manager creation failed (null handle)");
        }
        this.handle = handle;
    }

    // ─── Configuration (call before submitting nodes) ─────────────────────────

    /**
     * Sets the optimisation goal and score type.
     *
     * <p>Equivalent to {@code nm.set_goal(goal, score_type)} in C++.
     *
     * @param goal      whether to maximise or minimise the score
     * @param scoreType numeric representation of the score
     */
    public void setGoal(Goal goal, ScoreType scoreType) {
        GemPBANative.NodeManager.setGoal(handle, goal.ordinal(), scoreType.cppOrdinal());
    }

    /**
     * Overrides the load-balancing strategy at runtime.
     *
     * <p>Equivalent to {@code nm.set_balancing_policy(policy)} in C++.
     * Normally set once before submitting the seed node.
     *
     * @param policy the desired load-balancing strategy
     */
    public void setBalancingPolicy(BalancingPolicy policy) {
        GemPBANative.NodeManager.setBalancingPolicy(handle, policy.ordinal());
    }

    /**
     * Sets the number of worker threads in the thread pool.
     *
     * @param n thread-pool size; must be &gt; 0
     */
    public void setThreadPoolSize(int n) {
        if (n <= 0) throw new IllegalArgumentException("Thread pool size must be > 0, got: " + n);
        GemPBANative.NodeManager.setThreadPoolSize(handle, n);
    }

    /**
     * Submits a node to the thread pool for asynchronous execution.
     *
     * <p>Equivalent to {@code nm.try_local_submit(node)} in C++. The caller
     * should not use {@code node} after this call.
     *
     * @param node the node to submit; must not be null or already consumed
     */
    public boolean tryLocalSubmit(Node node) {
        try {
            return GemPBANative.NodeManager.tryLocalSubmit(handle, node.state.handle);
        } finally {
            // Prevent the JIT from releasing `node` before the native call
            // completes; if it did, the Cleaner could destroy the C++ wrapper
            // mid-call and trigger bad_weak_ptr from shared_from_this().
            Reference.reachabilityFence(node);
        }
    }

    // ─── Node submission ──────────────────────────────────────────────────────

    /**
     * Runs a node synchronously on the current thread.
     *
     * <p>Equivalent to {@code nm.forward(node)} in C++. Typically used to run
     * one branch inline while the other is submitted to the pool.
     *
     * @param node the node to execute immediately
     */
    public void forward(Node node) {
        try {
            GemPBANative.NodeManager.forward(handle, node.state.handle);
        } finally {
            Reference.reachabilityFence(node);
        }
    }

    // ─── Result reporting ─────────────────────────────────────────────────────

    /**
     * Attempts to update the global best result with a Java object (MT mode).
     *
     * <p>Equivalent to {@code nm.try_update_result(result, score)} in C++, which
     * stores the value in the {@code std::any} arm of the internal variant.
     * The score check is delegated to C++ (non-serializer overload); a Java lock
     * serialises the check-and-store pair so the stored object always corresponds
     * to the accepted score.
     *
     * @param result the candidate result object
     * @param score  the score associated with {@code result}
     * @param <T>    result type
     * @return {@code true} if the score improved and the result was stored
     */
    public <T> boolean tryUpdateResult(T result, Score score) {
        synchronized (m_javaResultLock) {
            boolean updated = GemPBANative.NodeManager.tryUpdateResultMT(
                    handle, score.rawBits(), score.typeOrdinal());
            if (updated) {
                m_javaResult = result;
            }
            return updated;
        }
    }

    /**
     * Attempts to update the global best result together with its serialised
     * value (MP / serialisation mode).
     *
     * <p>Equivalent to {@code nm.try_update_result(result, score, serializer)} in C++,
     * which stores the value in the {@code task_packet} arm of the internal variant.
     *
     * @param resultBytes serialised result value
     * @param score       the score to compare against the current best
     */
    public boolean tryUpdateResult(byte[] resultBytes, Score score) {
        return GemPBANative.NodeManager.tryUpdateResult(handle, resultBytes, score.rawBits(), score.typeOrdinal());
    }

    /**
     * Returns the best score found during the search.
     *
     * <p>Call after {@link #waitForCompletion()}.
     */
    public Score getScore() {
        long rawBits = GemPBANative.NodeManager.getScoreRawBits(handle);
        int cppOrdinal = GemPBANative.NodeManager.getScoreTypeOrdinal(handle);
        return Score.fromRaw(rawBits, ScoreType.fromCppOrdinal(cppOrdinal));
    }

    // ─── Result retrieval (call after waitForCompletion) ──────────────────────

    /**
     * Sets the initial best score (typically the worst possible value for the
     * chosen {@link Goal}).
     *
     * @param score starting score
     */
    public void setScore(Score score) {
        GemPBANative.NodeManager.setScore(handle, score.rawBits(), score.typeOrdinal());
    }

    /**
     * Returns the serialised best result bytes, or {@code null} if no byte-array
     * result was ever stored via {@link #tryUpdateResult(byte[], Score)}.
     *
     * <p>Call after {@link #waitForCompletion()}. Mirrors {@code get_result_bytes()}
     * in C++ (MP / serialisation mode).
     */
    public byte[] getResultBytes() {
        return GemPBANative.NodeManager.getResultBytes(handle);
    }

    /**
     * Returns the best result stored in MT mode as an {@link Optional}.
     *
     * <p>Equivalent to {@code nm.get_result<T>()} in C++, which reads from the
     * {@code std::any} arm of the internal variant. Call after
     * {@link #waitForCompletion()}.
     *
     * @param <T> expected result type
     * @return {@code Optional} containing the stored result, or empty if none was
     * stored via {@link #tryUpdateResult(Object, Score)}
     */
    @SuppressWarnings("unchecked")
    public <T> Optional<T> getResult() {
        return Optional.ofNullable((T) m_javaResult);
    }

    /**
     * Returns the deserialised best result, or {@code null} if none was stored.
     *
     * <p>Convenience wrapper over {@link #getResultBytes()} that applies
     * {@code deserializer} to the raw bytes. Mirrors {@code get_result<T>()}
     * in C++ (MT mode with byte-encoded results).
     *
     * @param deserializer converts {@code byte[]} → {@code T}
     * @param <T>          result type
     * @return the deserialised result, or {@code null}
     */
    public <T> T getResult(Deserializer<T> deserializer) {
        byte[] bytes = getResultBytes();
        return bytes != null ? deserializer.deserialize(bytes) : null;
    }

    /**
     * Generates a new unique integer ID via the underlying load balancer.
     *
     * <p>Equivalent to {@code nm.generate_unique_id()} in C++. Useful for
     * assigning stable IDs to serial runnables or other per-task data.
     *
     * @return a unique ID (non-negative)
     */
    public int generateUniqueId() {
        return GemPBANative.NodeManager.generateUniqueId(handle);
    }

    // ─── Statistics and diagnostics ──────────────────────────────────────────────

    /**
     * Returns the current wall-clock time in seconds since the epoch.
     *
     * <p>Equivalent to {@code gempba::node_manager::get_wall_time()} in C++ (static).
     */
    public static double getWallTime() {
        return GemPBANative.NodeManager.getWallTime(0L);
    }

    /**
     * Returns the MPI rank of this process, or -1 in MT-only mode.
     */
    public int rankMe() {
        return GemPBANative.NodeManager.rankMe(handle);
    }

    /**
     * Returns the total number of thread requests issued to the load balancer.
     */
    public long getThreadRequestCount() {
        return GemPBANative.NodeManager.getThreadRequestCount(handle);
    }

    /**
     * Returns the cumulative idle time of the thread pool in seconds.
     */
    public double getIdleTime() {
        return GemPBANative.NodeManager.getIdleTime(handle);
    }

    /**
     * Returns the load-balancing policy currently in use.
     */
    public BalancingPolicy getBalancingPolicy() {
        return BalancingPolicy.values()[GemPBANative.NodeManager.getBalancingPolicy(handle)];
    }

    /**
     * Tries to submit a node to a remote MPI process (MP mode only).
     *
     * <p>Equivalent to {@code nm.try_remote_submit(node, runnableId)} in C++.
     *
     * @param node       the node whose serialised args are forwarded
     * @param runnableId ID of the matching serial runnable on the remote worker
     * @return {@code true} if the node was successfully dispatched
     */
    public boolean tryRemoteSubmit(Node node, int runnableId) {
        try {
            return GemPBANative.NodeManager.tryRemoteSubmit(handle, node.state.handle, runnableId);
        } finally {
            Reference.reachabilityFence(node);
        }
    }

    // ─── Synchronisation ──────────────────────────────────────────────────────

    /**
     * Returns {@code true} if all submitted nodes have finished.
     *
     * <p>Equivalent to {@code nm.is_done()} in C++.
     */
    public boolean isDone() {
        return GemPBANative.NodeManager.isDone(handle);
    }

    /**
     * Blocks the calling thread until all submitted nodes have finished.
     *
     * <p>Equivalent to {@code nm.wait()} in C++.
     *
     * @throws InterruptedException if the calling thread is interrupted while waiting
     */
    public void waitForCompletion() throws InterruptedException {
        GemPBANative.NodeManager.waitForCompletion(handle);
    }

    // ─── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Releases the native node manager. Call only after {@link #waitForCompletion()}.
     */
    @Override
    public void close() {
        GemPBANative.NodeManager.destroy(handle);
    }
}
