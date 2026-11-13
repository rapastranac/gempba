package io.gempba.value;

/**
 * How a run ended, mirroring {@code gempba::end_reason}.
 *
 * <p>Ordinal values match the C++ enum definition exactly so they can be
 * passed directly through JNI without a translation table.
 */
public enum EndReason {
    /**
     * The search space was exhausted, so the result is proven.
     */
    COMPLETED,   // ordinal 0 == gempba::end_reason::COMPLETED
    /**
     * A stop was requested, so the result is the best one found before it.
     */
    INTERRUPTED  // ordinal 1 == gempba::end_reason::INTERRUPTED
}
