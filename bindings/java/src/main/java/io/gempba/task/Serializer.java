package io.gempba.task;

/**
 * Converts a value of type {@code A} to a byte array for transport across the
 * JNI boundary into the C++ {@code task_packet}.
 *
 * @param <A> the type to serialise
 */
@FunctionalInterface
public interface Serializer<A> {

    /**
     * Serialise {@code value} to a byte array.
     *
     * @param value the value to serialise; must not be {@code null}
     * @return a non-null byte array representing the value
     */
    byte[] serialize(A value);
}
