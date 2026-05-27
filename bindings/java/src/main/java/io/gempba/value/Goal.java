package io.gempba.value;

/**
 * Optimisation direction, mirroring {@code gempba::goal}.
 *
 * <p>Ordinal values match the C++ enum definition exactly so they can be
 * passed directly through JNI without a translation table.
 */
public enum Goal {
    /**
     * Keep the highest score seen so far.
     */
    MAXIMISE,   // ordinal 0 == gempba::MAXIMISE
    /**
     * Keep the lowest score seen so far.
     */
    MINIMISE    // ordinal 1 == gempba::MINIMISE
}
