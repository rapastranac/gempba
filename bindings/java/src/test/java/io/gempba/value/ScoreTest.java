package io.gempba.value;

import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertInstanceOf;

/**
 * Pure-Java tests for {@link Score} serialisation logic.
 * <p>
 * These tests do not load the native library. Their purpose is to verify that
 * {@link Score#rawBits()} produces exactly the bit pattern the C++ {@code from_raw()}
 * and {@code to_raw()} functions expect, and that {@link ScoreType#cppOrdinal()}
 * matches the C++ {@code score_type} enum (any mismatch silently breaks JNI transport).
 */
class ScoreTest {

    // ─── ScoreType C++ ordinals ───────────────────────────────────────────────
    // C++ enum: I32=0, U_I32=1, I64=2, U_I64=3, F32=4, F64=5, F128=6
    // Java only exposes the four types it can represent natively.

    @Test
    void score_type_cpp_ordinals_match_cpp_enum() {
        assertEquals(0, ScoreType.I32.cppOrdinal());
        assertEquals(2, ScoreType.I64.cppOrdinal());
        assertEquals(4, ScoreType.F32.cppOrdinal());
        assertEquals(5, ScoreType.F64.cppOrdinal());
    }

    @Test
    void from_cpp_ordinal_round_trips() {
        assertEquals(ScoreType.I32, ScoreType.fromCppOrdinal(0));
        assertEquals(ScoreType.I64, ScoreType.fromCppOrdinal(2));
        assertEquals(ScoreType.F32, ScoreType.fromCppOrdinal(4));
        assertEquals(ScoreType.F64, ScoreType.fromCppOrdinal(5));
    }

    // ─── Score.make() raw-bit encoding ────────────────────────────────────────

    @Test
    void make_int_produces_unsigned_long_bits() {
        Score s = Score.make(42);
        assertEquals(ScoreType.I32, s.type);
        assertEquals(42L, s.rawBits());
    }

    @Test
    void make_int_negative_value_zero_extends() {
        // -1 as int32 → raw bits are the unsigned-extended 0xFFFFFFFF
        Score s = Score.make(-1);
        assertEquals(ScoreType.I32, s.type);
        assertEquals(0xFFFFFFFFL, s.rawBits());
    }

    @Test
    void make_long_preserves_full_64_bits() {
        long value = Long.MIN_VALUE; // 0x8000000000000000
        Score s = Score.make(value);
        assertEquals(ScoreType.I64, s.type);
        assertEquals(value, s.rawBits());
    }

    @Test
    void make_float_uses_ieee754_bits() {
        float value = 1.5f;
        Score s = Score.make(value);
        assertEquals(ScoreType.F32, s.type);
        assertEquals((long) Float.floatToRawIntBits(value) & 0xFFFFFFFFL, s.rawBits());
    }

    @Test
    void make_double_uses_ieee754_bits() {
        double value = 1.5;
        Score s = Score.make(value);
        assertEquals(ScoreType.F64, s.type);
        assertEquals(Double.doubleToRawLongBits(value), s.rawBits());
    }

    // ─── typeOrdinal() uses cppOrdinal, not Java ordinal ─────────────────────

    @Test
    void type_ordinal_matches_cpp_ordinal() {
        assertEquals(ScoreType.I32.cppOrdinal(), Score.make(0).typeOrdinal());
        assertEquals(ScoreType.I64.cppOrdinal(), Score.make(0L).typeOrdinal());
        assertEquals(ScoreType.F32.cppOrdinal(), Score.make(0f).typeOrdinal());
        assertEquals(ScoreType.F64.cppOrdinal(), Score.make(0.0).typeOrdinal());
    }

    // ─── getValue() returns correct boxed type ────────────────────────────────

    @Test
    void get_value_i32_returns_integer() {
        assertInstanceOf(Integer.class, Score.make(7).getValue());
        assertEquals(7, Score.make(7).getValue());
    }

    @Test
    void get_value_i32_negative_round_trips() {
        assertEquals(-1, Score.make(-1).getValue());
    }

    @Test
    void get_value_i64_returns_long() {
        assertInstanceOf(Long.class, Score.make(Long.MAX_VALUE).getValue());
        assertEquals(Long.MAX_VALUE, Score.make(Long.MAX_VALUE).getValue());
    }

    @Test
    void get_value_f32_returns_float() {
        assertInstanceOf(Float.class, Score.make(3.14f).getValue());
        assertEquals(3.14f, (float) Score.make(3.14f).getValue(), 0f);
    }

    @Test
    void get_value_f64_returns_double() {
        assertInstanceOf(Double.class, Score.make(3.14).getValue());
        assertEquals(3.14, (double) Score.make(3.14).getValue(), 0.0);
    }

    // ─── asDouble() loose conversion ─────────────────────────────────────────

    @Test
    void as_double_converts_i32() {
        assertEquals(42.0, Score.make(42).asDouble(), 0.0);
    }

    @Test
    void as_double_converts_f32() {
        assertEquals(1.5, Score.make(1.5f).asDouble(), 1e-6);
    }

    // ─── toString() shows actual value ────────────────────────────────────────

    @Test
    void to_string_shows_value_not_hex() {
        assertEquals("Score{I32, 5}", Score.make(5).toString());
        assertEquals("Score{F64, 3.14}", Score.make(3.14).toString());
    }
}
