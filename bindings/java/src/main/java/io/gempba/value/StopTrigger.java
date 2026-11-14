package io.gempba.value;

/**
 * What requested the stop that interrupted a run, mirroring {@code gempba::stop_trigger}.
 *
 * <p>Ordinal values match the C++ enum definition exactly so they can be
 * passed directly through JNI without a translation table.
 */
public enum StopTrigger {
    /**
     * No stop was requested.
     */
    NONE,     // ordinal 0 == gempba::stop_trigger::NONE
    /**
     * The program called {@link io.gempba.GemPBA#requestStop()}.
     */
    CODE,     // ordinal 1 == gempba::stop_trigger::CODE
    /**
     * {@code SIGINT} or {@code SIGTERM} arrived.
     */
    SIGNAL,   // ordinal 2 == gempba::stop_trigger::SIGNAL
    /**
     * A stop control line arrived on the telemetry socket.
     */
    CONTROL   // ordinal 3 == gempba::stop_trigger::CONTROL
}
