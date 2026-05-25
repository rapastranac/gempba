package io.gempba.stats;

import org.junit.jupiter.api.Test;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Unit tests for {@link RankStats}.
 *
 * <p>All tests are pure Java — no JNI calls required.
 */
class RankStatsTest {

    // ─── Helpers ──────────────────────────────────────────────────────────────

    /**
     * Builds a RankStats with mixed Number subtypes, mirroring what the JNI produces.
     */
    private static RankStats rankWith(int rank, Object... pairs) {
        LinkedHashMap<String, Number> map = new LinkedHashMap<>();
        for (int i = 0; i < pairs.length; i += 2) {
            String key = (String) pairs[i];
            Object val = pairs[i + 1];
            if (val instanceof Number n) map.put(key, n);
        }
        return new RankStats(rank, map);
    }

    // ─── get() ────────────────────────────────────────────────────────────────

    @Test
    void get_returns_value_for_known_key() {
        RankStats rs = rankWith(1,
                "received_task_count", 42L,
                "idle_time", 1.234567);

        assertEquals(42L, rs.get("received_task_count"));
        assertEquals(1.234567, rs.get("idle_time").doubleValue(), 1e-9);
    }

    @Test
    void get_returns_null_for_unknown_key() {
        RankStats rs = rankWith(0, "elapsed_time", 3.5);
        assertNull(rs.get("nonexistent_key"));
    }

    // ─── Type fidelity ────────────────────────────────────────────────────────

    @Test
    void integer_stat_is_stored_as_integer() {
        RankStats rs = rankWith(0, "some_int", 7);
        assertInstanceOf(Integer.class, rs.get("some_int"));
        assertEquals(7, rs.get("some_int").intValue());
    }

    @Test
    void long_stat_is_stored_as_long() {
        RankStats rs = rankWith(0, "sent_task_count", 1_000_000L);
        assertInstanceOf(Long.class, rs.get("sent_task_count"));
        assertEquals(1_000_000L, rs.get("sent_task_count").longValue());
    }

    @Test
    void double_stat_is_stored_as_double() {
        RankStats rs = rankWith(0, "idle_time", 0.9876543);
        assertInstanceOf(Double.class, rs.get("idle_time"));
        assertEquals(0.9876543, rs.get("idle_time").doubleValue(), 1e-9);
    }

    // ─── forEach() ────────────────────────────────────────────────────────────

    @Test
    void forEach_visits_all_entries_in_insertion_order() {
        RankStats rs = rankWith(2,
                "received_task_count", 3L,
                "sent_task_count", 10L,
                "idle_time", 0.094);

        var keys = new java.util.ArrayList<String>();
        var values = new java.util.ArrayList<Number>();
        rs.forEach((k, v) -> {
            keys.add(k);
            values.add(v);
        });

        assertEquals(java.util.List.of("received_task_count", "sent_task_count", "idle_time"), keys);
        assertEquals(3L, values.get(0));
        assertEquals(10L, values.get(1));
        assertEquals(0.094, values.get(2).doubleValue(), 1e-9);
    }

    @Test
    void forEach_count_matches_number_of_entries() {
        RankStats rs = rankWith(0, "a", 1L, "b", 2.0, "c", 3);
        AtomicInteger count = new AtomicInteger(0);
        rs.forEach((k, v) -> count.incrementAndGet());
        assertEquals(3, count.get());
    }

    // ─── asMap() ──────────────────────────────────────────────────────────────

    @Test
    void asMap_returns_unmodifiable_view() {
        RankStats rs = rankWith(0, "elapsed_time", 3.0);
        Map<String, Number> map = rs.asMap();
        assertThrows(UnsupportedOperationException.class, () -> map.put("new_key", 0));
    }

    @Test
    void asMap_reflects_same_entries_as_get() {
        RankStats rs = rankWith(1, "received_task_count", 5L, "elapsed_time", 1.23);
        Map<String, Number> map = rs.asMap();
        assertEquals(rs.get("received_task_count"), map.get("received_task_count"));
        assertEquals(rs.get("elapsed_time"), map.get("elapsed_time"));
    }

    // ─── toString() ───────────────────────────────────────────────────────────

    @Test
    void toString_starts_with_rank_line() {
        RankStats rs = rankWith(3, "elapsed_time", 1.0);
        assertTrue(rs.toString().startsWith("rank 3"), "toString must start with 'rank <N>'");
    }

    @Test
    void toString_formats_long_as_integer() {
        RankStats rs = rankWith(0, "received_task_count", 42L);
        String out = rs.toString();
        assertTrue(out.contains("42"), "Long value must appear as a plain integer");
        assertFalse(out.contains("42."), "Long value must not appear with a decimal point");
    }

    @Test
    void toString_formats_integer_as_integer() {
        RankStats rs = rankWith(0, "some_flag", 1);
        String out = rs.toString();
        assertTrue(out.contains("1"), "Integer value must appear as a plain integer");
        assertFalse(out.contains("1."), "Integer value must not appear with a decimal point");
    }

    @Test
    void toString_formats_double_with_seven_decimal_places() {
        RankStats rs = rankWith(0, "idle_time", 0.0941204);
        String out = rs.toString();
        assertTrue(out.contains("0.0941204"), "Double must be formatted to 7 decimal places");
    }

    @Test
    void toString_formats_whole_double_as_integer() {
        // A Double that happens to be a whole number (e.g. elapsed_time = 3.0)
        // should print as "3", not "3.0000000"
        RankStats rs = rankWith(0, "elapsed_time", 3.0);
        String out = rs.toString();
        assertTrue(out.contains("3"), "Whole-number Double must appear as a plain integer");
        assertFalse(out.contains("3.0"), "Whole-number Double must not have unnecessary decimals");
    }

    // ─── rank field ───────────────────────────────────────────────────────────

    @Test
    void rank_field_matches_constructor_argument() {
        RankStats rs = rankWith(7, "elapsed_time", 0.1);
        assertEquals(7, rs.rank);
    }
}
