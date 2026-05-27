package io.gempba.task;

import io.gempba.core.Node;

/**
 * User-supplied branch-and-bound task.
 *
 * <p>This interface imposes a fixed three-position signature shape:
 * <pre>
 *   (long threadId,  Args args,  Node self)
 *    ^-- fixed        ^-- yours   ^-- fixed
 * </pre>
 * The middle position {@code Args} is <em>entirely under the user's control</em>:
 * it can be a primitive wrapper, a plain class, or — most commonly — a
 * {@code record} that bundles as many fields of as many different types as the
 * algorithm needs:
 *
 * <pre>{@code
 * // Any number of arguments, any types — bundle them in a record:
 * record SearchArgs(int depth, double bound, int[] weights, String label) {}
 *
 * NodeTask<SearchArgs, Void> task = (tid, args, self) ->
 *     explore(tid, args.depth(), args.bound(), args.weights(), args.label(), self);
 * }</pre>
 *
 * <p>In <strong>MP (multi-process / MPI) mode</strong> the {@code Args} bundle
 * is serialised to {@code byte[]} at node-creation time and deserialised on
 * whichever worker process picks the node up — serialisation is the mechanism,
 * not a bottleneck.
 *
 * <p>In <strong>MT (single-machine) mode</strong> prefer {@link ClosureTask}:
 * it captures arguments directly in the Java lambda with zero serialisation
 * and without the need to declare a wrapper type.
 *
 * <p>Implementations must be thread-safe: multiple threads may execute
 * instances of the same task concurrently with different arguments.
 *
 * @param <Args> the user's argument bundle — any type, carrying any number of
 *               fields; serialised to {@code byte[]} when crossing the JNI
 *               or MPI boundary
 * @param <R>    return type ({@link Void} for tasks that report results via
 *               {@link io.gempba.core.NodeManager#tryUpdateResult} rather than a return value)
 */
@FunctionalInterface
public interface NodeTask<Args, R> {

    /**
     * Execute one node of the search tree.
     *
     * @param threadId opaque identifier of the executing thread (mirrors
     *                 {@code std::thread::id} passed by the C++ framework)
     * @param args     the arguments for this node, deserialised from the
     *                 byte buffer that was stored when the node was created
     * @param self     execution context — use it to create and submit child nodes
     * @return the result of this branch, or {@code null} for void tasks
     */
    R execute(long threadId, Args args, Node self);
}
