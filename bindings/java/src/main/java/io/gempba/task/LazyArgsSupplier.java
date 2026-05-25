package io.gempba.task;

/**
 * Supplier for the lazy-node argument initialiser.
 *
 * <p>Called by the framework just before a lazy node is executed.
 * Return serialised argument bytes to allow execution, or {@code null}
 * to prune this node entirely.
 *
 * <p>Used in:
 * <ul>
 *   <li>{@code GemPBA.createLazyNode} — the returned bytes become the
 *       deserialized arguments passed to the {@link NodeTask}.</li>
 *   <li>{@code GemPBA.createLazyNode} — only the null/non-null distinction
 *       matters; the actual byte content is ignored (the closure already
 *       captures its arguments).</li>
 * </ul>
 */
@FunctionalInterface
public interface LazyArgsSupplier {

    /**
     * Returns argument bytes to proceed with execution, or {@code null} to prune.
     *
     * @return serialised arguments, or {@code null} to skip this node
     */
    byte[] get();
}
