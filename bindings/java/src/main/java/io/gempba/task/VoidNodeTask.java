package io.gempba.task;

import io.gempba.core.Node;

/**
 * Convenience specialisation of {@link NodeTask} for void-returning tasks.
 *
 * <p>Results are reported via {@link io.gempba.core.NodeManager#tryUpdateResult} rather than
 * a return value.  Using this interface avoids the boilerplate {@code return null}
 * that {@code NodeTask<Args, Void>} would otherwise require.
 *
 * <p>The required signature shape mirrors {@link NodeTask}:
 * <pre>
 *   (long threadId,  Args args,  Node self)
 *    ^-- fixed        ^-- yours   ^-- fixed
 * </pre>
 * {@code Args} is fully under the user's control — use a {@code record} to
 * bundle any number of arguments of any types:
 *
 * <pre>{@code
 * // Ordinary static method — any middle arguments, any types:
 * record SearchArgs(int depth, double bound, int[] weights, String label) {}
 *
 * static void explore(long threadId, SearchArgs args, Node self) {
 *     NodeManager  nm = GemPBA.getNodeManager();
 *     LoadBalancer lb = GemPBA.getLoadBalancer();
 *     if (args.depth() == 0) {
 *         nm.tryUpdateResult(Score.make(1));
 *         return;
 *     }
 *     SearchArgs next = new SearchArgs(args.depth() - 1, args.bound(), args.weights(), args.label());
 *     Node left  = GemPBA.createExplicitNode(lb, self, TreeSearch::explore, next, SER, DESER);
 *     Node right = GemPBA.createExplicitNode(lb, self, TreeSearch::explore, next, SER, DESER);
 *     nm.tryLocalSubmit(left);
 *     nm.forward(right);
 * }
 *
 * GemPBA.createSeedNode(lb, (VoidNodeTask<SearchArgs>) TreeSearch::explore, initial, SER, DESER);
 * }</pre>
 *
 * <p>For <strong>MT-only (single-machine)</strong> work where no serialisation
 * is needed, prefer {@link ClosureTask}: it captures all arguments directly in
 * the lambda without declaring a wrapper type.
 *
 * @param <Args> the user's argument bundle — any type, carrying any number of
 *               fields; serialised to {@code byte[]} when crossing the JNI
 *               or MPI boundary
 */
@FunctionalInterface
public interface VoidNodeTask<Args> extends NodeTask<Args, Void> {

    /**
     * Execute one node of the search tree.
     *
     * @param threadId opaque executing-thread identifier
     * @param args     deserialised node arguments
     * @param self     execution context for creating child nodes
     */
    void run(long threadId, Args args, Node self);

    /**
     * Bridges {@link NodeTask#execute} to {@link #run}, returning {@code null}.
     */
    @Override
    default Void execute(long threadId, Args args, Node self) {
        run(threadId, args, self);
        return null;
    }
}
