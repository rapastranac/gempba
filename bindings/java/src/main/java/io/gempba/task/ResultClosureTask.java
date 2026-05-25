package io.gempba.task;

import io.gempba.core.Node;

/**
 * A node task for <strong>MT (multithreading) mode</strong> whose body
 * computes and returns a typed result, paired with a {@link Serializer} that
 * encodes that result for retrieval via {@link Node#getResult()}.
 *
 * <p>The Java equivalent of a C++ lambda with a non-void return:
 * <pre>{@code
 * Node node = GemPBA.createLazyNode(lb, parent, gate,
 *     (tid, self) -> mvc(tid, depth, graph, self),  // returns Graph
 *     GRAPH_SERIALIZER);
 * nm.tryLocalSubmit(node);
 * byte[] bytes = node.getResult();                   // blocks
 * Graph result = GRAPH_DESERIALIZER.deserialize(bytes);
 * }</pre>
 *
 * <p>Like {@link ClosureTask}, arguments to the algorithm are captured in the
 * Java lambda's closure rather than serialised through the JNI byte-array
 * boundary — so this is MT-only.  For MP the equivalent flows through
 * {@link NodeTask} with explicit args + result serialisers.
 *
 * <p>Returning {@code null} from {@link #run} is treated as "no result"; a
 * subsequent {@link Node#getResult()} also returns {@code null}.
 *
 * @param <R> result type produced by the task body
 */
@FunctionalInterface
public interface ResultClosureTask<R> {

    /**
     * Executes this node's work and returns the typed result.
     *
     * @param threadId hash of the executing thread's {@code std::thread::id}
     * @param self     the executing node; pass it as parent when creating
     *                 children via {@link io.gempba.GemPBA#createExplicitNode}
     * @return the typed result, or {@code null} for "no result"
     */
    R run(long threadId, Node self);
}
