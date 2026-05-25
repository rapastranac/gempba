package io.gempba.task;

/**
 * Reconstructs a value of type {@code A} from a byte array received from the
 * C++ {@code task_packet}.
 *
 * @param <A> the type to deserialise into
 */
@FunctionalInterface
public interface Deserializer<A> {

    /**
     * Deserialise {@code bytes} into a value.
     *
     * @param bytes the raw bytes; must not be {@code null}
     * @return the reconstructed value
     */
    A deserialize(byte[] bytes);
}
