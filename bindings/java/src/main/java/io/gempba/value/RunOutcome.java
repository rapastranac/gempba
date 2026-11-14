package io.gempba.value;

/**
 * How a run ended, mirroring {@code gempba::run_outcome}, as
 * {@link io.gempba.scheduler.Scheduler#run} returns it.
 *
 * @param reason  whether the run completed or was interrupted
 * @param trigger what requested the stop; {@link StopTrigger#NONE} for a completed run
 * @param signal  the signal's number when {@code trigger} is {@link StopTrigger#SIGNAL}; 0 otherwise
 */
public record RunOutcome(EndReason reason, StopTrigger trigger, int signal) {

    /**
     * Rebuilds an outcome from the {@code {reason, trigger, signal}} ordinals the native library returns.
     *
     * @param ordinals the reason's ordinal, the trigger's ordinal and the signal number, in that order
     * @return the outcome
     */
    public static RunOutcome fromOrdinals(int[] ordinals) {
        return new RunOutcome(EndReason.values()[ordinals[0]], StopTrigger.values()[ordinals[1]], ordinals[2]);
    }

    /**
     * @return whether a stop interrupted the run, so its result is the best found before it rather than a proven one
     */
    public boolean isInterrupted() {
        return reason == EndReason.INTERRUPTED;
    }
}
