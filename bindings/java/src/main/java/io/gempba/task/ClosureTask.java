package io.gempba.task;

import io.gempba.core.Node;

/**
 * A node task for <strong>MT (multithreading / single-machine) mode</strong>
 * whose arguments are captured in the Java lambda closure rather than
 * serialised through the JNI byte-array boundary.
 *
 * <p>This is the Java equivalent of a C++ lambda with a variadic capture list.
 * Any number of arguments of any type can be bundled without declaring a
 * wrapper type and without any serialisation cost:
 *
 * <pre>{@code
 * int depth = 3;
 * List<Integer> partial = new ArrayList<>();
 * MyGraph graph = loadGraph();
 *
 * Node node = GemPBA.createExplicitNode(lb, parent,
 *     (tid, self) -> myAlgorithm(tid, depth, partial, graph, self));
 * }</pre>
 *
 * <p>Nothing is serialised — the closure object lives on the Java heap and the
 * JNI shim stores only the {@code jobject} global reference.  For work that
 * must cross MPI process boundaries use {@link NodeTask} / {@link VoidNodeTask}
 * with explicit serialisers instead.
 *
 * <p>The framework invokes {@link #run} with the executing thread's id and a
 * self-reference; create child nodes inside the body via
 * {@link io.gempba.GemPBA#createExplicitNode}.
 *
 * <pre>{@code
 * NodeManager nm = ...;
 * LoadBalancer lb = ...;
 *
 * void explore(long tid, int depth, List<Double> data, Node self) {
 *     nm.tryUpdateResult(Score.make(depth));
 *     if (depth < maxDepth) {
 *         List<Double> left  = new ArrayList<>(data);
 *         List<Double> right = new ArrayList<>(data);
 *         Node ln = GemPBA.createExplicitNode(lb, self,
 *             (t, s) -> explore(t, depth + 1, left,  s));
 *         Node rn = GemPBA.createExplicitNode(lb, self,
 *             (t, s) -> explore(t, depth + 1, right, s));
 *         nm.tryLocalSubmit(ln);
 *         nm.forward(rn);
 *     }
 * }
 * }</pre>
 */
@FunctionalInterface
public interface ClosureTask {

    /**
     * Executes this node's work.
     *
     * @param threadId hash of the executing thread's {@code std::thread::id}
     * @param self     the executing node; pass it as parent when creating
     *                 children via {@link io.gempba.GemPBA#createExplicitNode}
     */
    void run(long threadId, Node self);
}
