package io.gempba.core;

import io.gempba.internal.GemPBANative;

import java.lang.ref.Cleaner;

/**
 * Java representation of one node in the gempba search tree.
 *
 * <p>Mirrors {@code gempba::node} in C++: a type-erased handle to an
 * underlying {@code node_core} object.  The node carries no knowledge of
 * argument or result types — all serialisation/deserialisation logic is
 * captured in the lambdas supplied to the factory methods
 * ({@link io.gempba.GemPBA#createSeedNode}, {@link io.gempba.GemPBA#createExplicitNode},
 * {@link io.gempba.GemPBA#createExplicitNode}, etc.).
 *
 * <h2>Roles</h2>
 * <ol>
 *   <li><strong>Submission handle</strong> — pass to
 *       {@link NodeManager#tryLocalSubmit} or {@link NodeManager#forward}.</li>
 *   <li><strong>Parent reference</strong> — pass as {@code parent} to any
 *       {@code createExplicitNode} / {@code createLazyNode} factory when
 *       creating child nodes inside a task body.</li>
 * </ol>
 *
 * <h2>Creating child nodes</h2>
 * <p>Inside a {@link io.gempba.task.NodeTask} or {@link io.gempba.task.ClosureTask}, use the factory methods
 * directly rather than a {@code createChild} helper:
 * <pre>{@code
 * // Serialisation-mode child (same task, new args):
 * Node left = GemPBA.createExplicitNode(lb, self, MySearch::explore, depth + 1, SER, DESER);
 *
 * // Closure-mode child (args captured in lambda):
 * Node right = GemPBA.createExplicitNode(lb, self,
 *     (tid, s) -> explore(tid, depth + 1, data, s));
 * }</pre>
 */
public final class Node implements AutoCloseable {

    // ─── Internal types ───────────────────────────────────────────────────────

    public static final byte[] EMPTY_BYTES = new byte[0];

    private static final Cleaner CLEANER = Cleaner.create();

    /**
     * Off-heap state carried by the {@link Cleaner} action.  Holds the C++
     * wrapper handle and ownership flag.  Must NOT reference the enclosing
     * {@link Node} (that would defeat the cleaner — the Node would never be
     * unreachable).
     */
    public static final class State implements Runnable {
        public volatile long handle = 0L;
        public boolean owned = true;

        @Override
        public void run() {
            long h = handle;
            handle = 0L;
            if (owned && h != 0L) {
                GemPBANative.Node.destroy(h);
            }
        }
    }

    /**
     * Handle of the load balancer this node belongs to.
     */
    public final long lbHandle;

    // ─── Fields ───────────────────────────────────────────────────────────────
    /**
     * Type-erased execution logic.
     */
    private final Executor executor;
    /**
     * Optional lazy-initialiser.  Returns {@code null} to prune, non-null
     * bytes (possibly empty) to proceed.  A {@code null} field means
     * "non-lazy: always proceed with empty args".
     */
    private final java.util.function.Supplier<byte[]> lazyInitSupplier;

    /**
     * Carries the C++ {@code java_node*} pointer and is the action invoked
     * by the {@link Cleaner} when this Java {@code Node} becomes unreachable.
     */
    public final State state = new State();

    private final Cleaner.Cleanable cleanable;

    /**
     * Non-lazy node (explicit / seed / dummy / closure).
     */
    public Node(long lbHandle, Executor executor) {
        this.lbHandle = lbHandle;
        this.executor = executor;
        this.lazyInitSupplier = null;
        this.cleanable = CLEANER.register(this, state);
    }

    // ─── Construction ─────────────────────────────────────────────────────────

    /**
     * Lazy node (MT closure-mode or MP serialisation-mode).
     */
    public Node(long lbHandle, Executor executor,
                java.util.function.Supplier<byte[]> lazyInitSupplier) {
        this.lbHandle = lbHandle;
        this.executor = executor;
        this.lazyInitSupplier = lazyInitSupplier;
        this.cleanable = CLEANER.register(this, state);
    }

    /**
     * Creates a minimal execution-context {@code Node} with a known handle.
     *
     * <p>Used inside factory lambdas to build the {@code self} argument
     * passed to task bodies.  The returned node's executor is never invoked;
     * the node exists solely so the task body can pass it as the
     * {@code parent} argument to child-creation factory methods.
     *
     * <p>The returned view does <em>not</em> own the underlying wrapper:
     * its cleaner will not call {@code destroy}.
     */
    public static Node ofHandle(long lbHandle, long handle) {
        Node n = new Node(lbHandle, (tid, args, h) -> null);
        n.state.owned = false;
        n.state.handle = handle;
        return n;
    }

    /**
     * Native pointer to the C++ {@code java_node} wrapper.  Zero means the
     * node has been closed or not yet initialised.
     */
    public long handle() {
        return state.handle;
    }

    /**
     * Blocks until this node's runnable has finished and returns the
     * serialised result bytes the runnable wrote to its output buffer.
     * Returns {@code null} when the runnable was pruned or produced no
     * result (void tasks, lazy nodes whose gate returned false, etc.).
     *
     * <p>Equivalent to {@code gempba::node::get_any_result()} on the C++ side
     * — the std::any is unwrapped to the runnable's serialised bytes.  Pair
     * this with the {@link io.gempba.task.Deserializer} you used to encode the runnable's
     * return value to recover the typed result:
     * <pre>{@code
     * Node n = GemPBA.createLazyNode(lb, parent, gate, task, GRAPH_SER);
     * nm.tryLocalSubmit(n);
     * byte[] bytes = n.getResult();
     * Graph result = GRAPH_DESER.deserialize(bytes);
     * }</pre>
     *
     * @return serialised result bytes, or {@code null} for void / pruned nodes
     */
    public byte[] getResult() {
        return GemPBANative.Node.getResult(state.handle);
    }

    /**
     * Called by the JNI shim for lazy nodes just before execution.
     * Returns {@code null} to prune; any non-null array (including empty)
     * to proceed — the bytes become the args packet passed to
     * {@link #_execute}.
     *
     * <strong>Not part of the public API.</strong>
     */
    byte[] _lazyInitArgs() {
        return (lazyInitSupplier != null) ? lazyInitSupplier.get() : EMPTY_BYTES;
    }

    // ─── JNI callbacks — package-private, called by C++ ─────────────────────

    /**
     * Called by the JNI shim when this node is executed by the framework.
     * Delegates to the closure captured at creation time.
     *
     * <strong>Not part of the public API.</strong>
     */
    byte[] _execute(long threadId, byte[] argsBytes, long selfCppHandle) {
        try {
            return executor.execute(threadId, argsBytes, selfCppHandle);
        } catch (Throwable t) {
            System.err.println("[gempba] Node._execute(threadId=" + threadId
                    + ", argsBytes=" + (argsBytes == null ? "null" : ("byte[" + argsBytes.length + "]"))
                    + ", selfCppHandle=" + selfCppHandle + ") threw:");
            t.printStackTrace(System.err);
            System.err.flush();
            if (t instanceof RuntimeException re) throw re;
            if (t instanceof Error err) throw err;
            throw new RuntimeException(t);
        }
    }

    /**
     * Releases the underlying C++ node and the associated JNI global reference.
     *
     * <p>After {@code close()}, this object must not be used.
     */
    @Override
    public void close() {
        cleanable.clean();
    }

    // ─── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Type-erased execution body stored inside every node.
     *
     * <p>Implementations are created by factory methods; they capture all
     * type-specific logic (task, serializer, deserializer) in the closure.
     */
    @FunctionalInterface
    public interface Executor {
        /**
         * Invoked by the JNI shim when the framework executes this node.
         *
         * @param threadId   hash of the executing {@code std::thread::id}
         * @param argsBytes  serialised arguments (null for closure-mode nodes)
         * @param selfHandle native pointer to the C++ node; pass to
         *                   {@link Node#ofHandle} to obtain the execution context
         * @return serialised result bytes, or {@code null}
         */
        byte[] execute(long threadId, byte[] argsBytes, long selfHandle);
    }
}
