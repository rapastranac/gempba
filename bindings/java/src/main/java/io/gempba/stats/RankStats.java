package io.gempba.stats;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.function.BiConsumer;

/**
 * Statistics for one MPI rank, collected after {@code Scheduler.synchronizeStats()}.
 *
 * <p>Wraps the fields produced by the C++ stats visitor as a
 * {@code LinkedHashMap<String, Number>} keyed by the C++ field names.  Values
 * preserve their original C++ types: integer fields arrive as {@link Long} or
 * {@link Integer}; floating-point fields arrive as {@link Double}.  Because the
 * data is name-driven rather than positional, any new fields added to the C++
 * stats class automatically appear here — no Java changes required.
 *
 * <p>Obtain instances via {@code Scheduler.getStats()}.
 */
public final class RankStats {

    /**
     * MPI rank this object describes.
     */
    public final int rank;

    /**
     * Insertion-ordered, unmodifiable view of the stat name → value pairs.
     */
    private final Map<String, Number> values;

    /**
     * Binding-internal constructor (called by io.gempba.scheduler.Scheduler when
     * unpacking stats from the JNI bridge).  Public so cross-package access works;
     * not part of the user-facing API — typical callers receive RankStats from
     * {@code Scheduler.getStats()}.
     */
    public RankStats(int rank, LinkedHashMap<String, Number> values) {
        this.rank = rank;
        this.values = Collections.unmodifiableMap(values);
    }

    // ─── Access ───────────────────────────────────────────────────────────────

    /**
     * Returns the value for the given stat name, or {@code null} if the name is
     * not present.
     */
    public Number get(String name) {
        return values.get(name);
    }

    /**
     * Iterates every stat as a {@code (name, value)} pair in insertion order
     * (the order in which the C++ visitor populates them).
     */
    public void forEach(BiConsumer<String, Number> action) {
        values.forEach(action);
    }

    /**
     * Returns an unmodifiable view of the underlying stat map, suitable for
     * stream operations or external inspection.
     */
    public Map<String, Number> asMap() {
        return values;
    }

    // ─── Display ──────────────────────────────────────────────────────────────

    /**
     * Formats the stats in the same style as the C++ spdlog output:
     * integer values are printed as whole numbers, floating-point values with
     * 7 decimal places.
     *
     * <pre>{@code
     * rank 1
     *   received_task_count:   3
     *   sent_task_count:       10
     *   idle_time:             0.0941204
     *   elapsed_time:          3.0367056
     * }</pre>
     */
    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder(String.format("rank %d", rank));
        values.forEach((name, value) -> {
            String formatted;
            if (value instanceof Double d) {
                formatted = (d == Math.floor(d) && !Double.isInfinite(d))
                        ? Long.toString(d.longValue())
                        : String.format("%.7f", d);
            } else {
                formatted = value.toString();
            }
            sb.append(String.format("%n  %-26s%s", name + ":", formatted));
        });
        return sb.toString();
    }
}
