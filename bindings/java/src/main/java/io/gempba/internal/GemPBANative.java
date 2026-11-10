package io.gempba.internal;

/**
 * Raw JNI declarations for the {@code libgempba_jni} native library.
 *
 * <p>This class is an internal implementation detail. All public API lives in
 * the {@code io.gempba} package. Do not depend on this class directly.
 *
 * <h2>Structure</h2>
 * <p>The class mirrors the {@code gempba.hpp} namespace / class hierarchy.
 * Nested classes are added incrementally as each C++ class is bound:
 * <ul>
 *   <li>Top-level methods → {@code gempba::} global free functions</li>
 *   <li>{@link Node} → {@code gempba::node} destructor</li>
 *   <li>{@link LoadBalancer} → {@code gempba::load_balancer}</li>
 *   <li>{@link MT} → {@code gempba::mt} factory functions (load balancer)</li>
 * </ul>
 *
 * <h2>JNI naming</h2>
 * <p>Each {@code native} method corresponds to a C symbol whose name encodes
 * the full Java class path. Inner classes use {@code $} → {@code _00024} in
 * the mangled name, e.g. {@code GemPBANative$LoadBalancer.destroy} maps to
 * {@code Java_io_gempba_internal_GemPBANative_00024LoadBalancer_destroy}.
 *
 * <h2>Handle convention</h2>
 * <p>Every {@code long} parameter or return value labelled {@code *Handle} is
 * a C++ pointer cast to {@code jlong} via {@code reinterpret_cast<jlong>(ptr)}.
 */
public final class GemPBANative {

    /**
     * Loaded during class initialisation; accessed by nested classes to force
     * this class — and therefore the native library — to be initialised first.
     * Must not be a compile-time constant (non-primitive type avoids inlining).
     */
    public static final Object LOADED;

    static {
        NativeLoader.load("gempba_jni");
        LOADED = Boolean.TRUE;
    }

    private GemPBANative() {
    }

    // ─── gempba:: global free functions ──────────────────────────────────────

    /**
     * Creates a placeholder node with no runnable.
     */
    public static native long createDummyNode(long lbHandle);

    /**
     * Creates the root seed node.
     */
    public static native long createSeedNode(long lbHandle, Object callback, byte[] argsBytes);

    /**
     * Returns the handle of the singleton node manager.
     */
    public static native long getNodeManagerHandle();

    /**
     * Returns the handle of the singleton load balancer.
     */
    public static native long getLoadBalancerHandle();

    public static native int shutdown();

    /**
     * Creates the load balancer.  Wraps {@code gempba_create_load_balancer}.
     *
     * @param policyOrdinal   {@link io.gempba.value.BalancingPolicy} ordinal
     * @param schedulerHandle a {@link io.gempba.scheduler.Scheduler} handle; {@code 0} for a single-process run
     */
    public static native long createLoadBalancer(int policyOrdinal, long schedulerHandle);

    /**
     * Creates the node manager backed by {@code lbHandle}.  Wraps {@code gempba_create_node_manager}.
     *
     * @param lbHandle        load-balancer handle from {@link #createLoadBalancer}
     * @param schedulerHandle a {@link io.gempba.scheduler.Scheduler} handle; {@code 0} for a single-process run
     */
    public static native long createNodeManager(long lbHandle, long schedulerHandle);

    /**
     * Creates an explicit child node whose serialised args stay inside it, so it can run on another rank.
     * Wraps {@code gempba_create_explicit_node}.
     */
    public static native long createExplicitNode(long lbHandle, long parentHandle, Object callback, byte[] argsBytes);

    /**
     * Creates a lazy child node; arguments are produced by {@code callback._lazyInitArgs()}.  Wraps
     * {@code gempba_create_lazy_node}.
     */
    public static native long createLazyNode(long lbHandle, long parentHandle, Object callback, byte[] argsBytes);

    // ─── gempba::node ─────────────────────────────────────────────────────────

    /**
     * Destructor for the heap-allocated {@code java_node} wrapper.
     */
    public static final class Node {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private Node() {
        }

        /**
         * Releases the heap-allocated C++ node and its JNI global reference.
         */
        public static native void destroy(long handle);

        /**
         * Blocks until the node's runnable has finished and returns the
         * serialised result bytes.  Returns {@code null} if the runnable was
         * pruned or returned no result.
         *
         * <p>Mirrors the {@code gempba_node_get_result} ABI entry — see
         * {@code include/gempba/cabi/gempba.h} for the contract.
         */
        public static native byte[] getResult(long handle);
    }

    // ─── gempba::load_balancer ────────────────────────────────────────────────

    /**
     * JNI binding for {@code gempba::load_balancer}.
     */
    public static final class LoadBalancer {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private LoadBalancer() {
        }

        /**
         * Releases the load balancer.
         * In practice a no-op: the object is owned by the gempba singleton
         * and freed by {@code gempba::shutdown()}.
         */
        public static native void destroy(long handle);
    }

    // ─── gempba::node_manager ─────────────────────────────────────────────────

    /**
     * JNI binding for {@code gempba::node_manager}.
     */
    public static final class NodeManager {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private NodeManager() {
        }

        // configuration
        public static native void setGoal(long handle, int goalOrdinal, int scoreTypeOrdinal);

        public static native void setBalancingPolicy(long handle, int policyOrdinal);

        public static native void setThreadPoolSize(long handle, int n);

        // submission
        public static native boolean tryLocalSubmit(long handle, long nodeHandle);

        public static native void forward(long handle, long nodeHandle);

        // result reporting
        public static native boolean tryUpdateResultMT(long handle, long rawBits, int typeOrdinal);

        public static native boolean tryUpdateResult(long handle, byte[] resultBytes, long rawBits, int typeOrdinal);

        // result retrieval
        public static native long getScoreRawBits(long handle);

        public static native int getScoreTypeOrdinal(long handle);

        public static native void setScore(long handle, long rawBits, int typeOrdinal);

        public static native byte[] getResultBytes(long handle);

        // utilities
        public static native int generateUniqueId(long handle);

        public static native double getWallTime(long handle);

        public static native int rankMe(long handle);

        public static native long getThreadRequestCount(long handle);

        public static native double getIdleTime(long handle);

        public static native int getBalancingPolicy(long handle);

        // MP remote submission
        public static native boolean tryRemoteSubmit(long handle, long nodeHandle, int runnableId);

        // synchronisation
        public static native boolean isDone(long handle);

        public static native void waitForCompletion(long handle);

        // lifecycle
        public static native void destroy(long handle);
    }

    // ─── gempba::serial_runnable ─────────────────────────────────────────────

    /**
     * JNI binding for {@code gempba::serial_runnable}.
     */
    public static final class SerialRunnable {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private SerialRunnable() {
        }

        /**
         * Creates a heap-allocated {@code java_serial_runnable} wrapper.
         *
         * @param id           integer ID used by the scheduler worker's runnables map
         * @param callback     the Java {@code SerialRunnable} instance whose
         *                     {@code _execute} method is invoked by the C++ runnable
         * @param returnsValue {@code false} → void runnable (result ignored);
         *                     {@code true}  → non-void runnable (result bytes forwarded to center)
         * @return opaque handle to the heap-allocated wrapper
         */
        public static native long create(int id, Object callback, boolean returnsValue);

        /**
         * Releases the heap-allocated wrapper and its JNI global reference.
         */
        public static native void destroy(long handle);

        /**
         * Invokes the serial runnable against the given node manager.
         *
         * <p>For void runnables the callback runs asynchronously on a thread-pool
         * thread and this method returns {@code null} immediately.
         * For non-void runnables this method blocks until the result future
         * resolves and returns the serialised result bytes.
         *
         * @param srHandle  handle returned by {@link #create}
         * @param nmHandle  handle of the node manager supplying the thread pool
         * @param argsBytes serialised arguments passed to the callback
         * @return result bytes for non-void runnables; {@code null} for void runnables
         */
        public static native byte[] invoke(long srHandle, long nmHandle, byte[] argsBytes);
    }

    // ─── gempba::scheduler (MP) ──────────────────────────────────────────────

    /**
     * JNI binding for {@code gempba::scheduler}.
     */
    public static final class Scheduler {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private Scheduler() {
        }

        public static native void barrier(long handle);

        public static native int rankMe(long handle);

        public static native int worldSize(long handle);

        public static native void setGoal(long handle, int goalOrdinal, int scoreTypeOrdinal);

        public static native double elapsedTime(long handle);

        public static native void synchronizeStats(long handle);

        /**
         * Returns {@code Object[] { String[] names, double[worldSize][n] values }}.
         * Names and values travel together so new C++ fields surface automatically
         * in {@link io.gempba.stats.RankStats} without any Java wrapper change.
         * Call only from rank 0 after {@link #synchronizeStats}.
         */
        public static native Object[] getStats(long handle);

        /**
         * Whether this rank is the root, rank 0.
         */
        public static native boolean isRoot(long handle);

        /**
         * Runs this rank's part of the search; blocks until the whole run has ended.
         *
         * @param handle         scheduler handle
         * @param nmHandle       handle to the local node manager
         * @param ids            runnable IDs (parallel array with {@code srHandles})
         * @param srHandles      handles of the registered {@link SerialRunnable}s
         * @param seed           the root's first task; {@code null} on every other rank
         * @param seedRunnableId runnable ID for the seed; ignored when {@code seed} is {@code null}
         */
        public static native void run(long handle, long nmHandle, int[] ids, long[] srHandles,
                                      byte[] seed, int seedRunnableId);

        /**
         * Returns the best result bytes, empty if none.  Root only.
         */
        public static native byte[] getResult(long handle);

        /**
         * Returns per-rank result bytes, {@code null} where a rank found none.  Root only.
         */
        public static native byte[][] getAllResults(long handle);
    }

    // ─── gempba::telemetry (process-wide flag) ───────────────────────────────

    /**
     * JNI binding for {@code gempba::telemetry::enable / disable / is_enabled / configure_port}.
     *
     * <p>The flag is process-local and noexcept on the C++ side; in
     * multiprocessing builds the user is expected to call these symmetrically
     * across all ranks (the C++ docs explain why).
     */
    public static final class Telemetry {

        static {
            var ignored = GemPBANative.LOADED;
        }

        private Telemetry() {
        }

        public static native void enable();

        public static native void disable();

        public static native boolean isEnabled();

        public static native void configurePort(int port);
    }
}
