package io.gempba.value;

/**
 * A typed numeric score used to track the best result found during search.
 *
 * <p>Mirrors the supported subset of {@code gempba::score}. Four types are
 * available; the C++ library additionally supports {@code U_I32}, {@code U_I64},
 * and {@code F128} — use C++ directly if those are required.
 *
 * <h2>Factory methods</h2>
 * <pre>{@code
 * Score.make(0)    // I32 — signed 32-bit integer
 * Score.make(1L)   // I64 — signed 64-bit integer
 * Score.make(0.5f) // F32 — 32-bit float
 * Score.make(0.5)  // F64 — 64-bit double
 * }</pre>
 *
 * <h2>Retrieving the value</h2>
 * <pre>{@code
 * Number v = score.getValue();   // returns Integer / Long / Float / Double
 * int    i = score.asInt();      // loose conversion to int
 * long   l = score.asLong();     // loose conversion to long
 * float  f = score.asFloat();    // loose conversion to float
 * double d = score.asDouble();   // loose conversion to double
 * }</pre>
 */
public final class Score {

    /**
     * Raw bit pattern of the value, matching gempba::score's payload layout.
     */
    final long rawBits;

    /**
     * The score type; never {@code null}.
     */
    public final ScoreType type;

    private Score(long rawBits, ScoreType type) {
        this.rawBits = rawBits;
        this.type = type;
    }

    // ─── Factory methods ──────────────────────────────────────────────────────

    /**
     * Creates an {@code I32} score from a signed 32-bit integer.
     */
    public static Score make(int value) {
        return new Score(Integer.toUnsignedLong(value), ScoreType.I32);
    }

    /**
     * Creates an {@code I64} score from a signed 64-bit integer.
     */
    public static Score make(long value) {
        return new Score(value, ScoreType.I64);
    }

    /**
     * Creates an {@code F32} score from a 32-bit float.
     */
    public static Score make(float value) {
        return new Score(Integer.toUnsignedLong(Float.floatToRawIntBits(value)), ScoreType.F32);
    }

    /**
     * Creates an {@code F64} score from a 64-bit double.
     */
    public static Score make(double value) {
        return new Score(Double.doubleToRawLongBits(value), ScoreType.F64);
    }

    // ─── Package-private factory (used by NodeManager.getScore) ──────────────

    public static Score fromRaw(long rawBits, ScoreType type) {
        return new Score(rawBits, type);
    }

    // ─── Value retrieval ──────────────────────────────────────────────────────

    /**
     * Returns the stored value as its natural Java {@link Number} subtype.
     *
     * <p>Mirrors {@code score::get<T>()} in C++ — the returned object's runtime
     * type matches the {@link ScoreType}:
     * <ul>
     *   <li>{@code I32} → {@link Integer}
     *   <li>{@code I64} → {@link Long}
     *   <li>{@code F32} → {@link Float}
     *   <li>{@code F64} → {@link Double}
     * </ul>
     */
    public Number getValue() {
        return switch (type) {
            case I32 -> (int) rawBits;
            case I64 -> rawBits;
            case F32 -> Float.intBitsToFloat((int) rawBits);
            case F64 -> Double.longBitsToDouble(rawBits);
        };
    }

    /**
     * Returns the stored value converted to {@code int}.
     *
     * <p>Mirrors {@code score::get_loose<int32_t>()} in C++. Floating-point
     * values are truncated toward zero; {@code I64} is narrowed.
     */
    public int asInt() {
        return switch (type) {
            case I32 -> (int) rawBits;
            case I64 -> (int) rawBits;
            case F32 -> (int) Float.intBitsToFloat((int) rawBits);
            case F64 -> (int) Double.longBitsToDouble(rawBits);
        };
    }

    /**
     * Returns the stored value converted to {@code long}.
     *
     * <p>Mirrors {@code score::get_loose<int64_t>()} in C++.
     */
    public long asLong() {
        return switch (type) {
            case I32 -> (int) rawBits;   // sign-extend
            case I64 -> rawBits;
            case F32 -> (long) Float.intBitsToFloat((int) rawBits);
            case F64 -> (long) Double.longBitsToDouble(rawBits);
        };
    }

    /**
     * Returns the stored value converted to {@code float}.
     *
     * <p>Mirrors {@code score::get_loose<float>()} in C++.
     */
    public float asFloat() {
        return switch (type) {
            case I32 -> (int) rawBits;
            case I64 -> rawBits;
            case F32 -> Float.intBitsToFloat((int) rawBits);
            case F64 -> (float) Double.longBitsToDouble(rawBits);
        };
    }

    /**
     * Returns the stored value converted to {@code double}.
     *
     * <p>Mirrors {@code score::get_loose<double>()} in C++.
     */
    public double asDouble() {
        return switch (type) {
            case I32 -> (int) rawBits;
            case I64 -> rawBits;
            case F32 -> Float.intBitsToFloat((int) rawBits);
            case F64 -> Double.longBitsToDouble(rawBits);
        };
    }

    // ─── Internal helpers (JNI boundary) ─────────────────────────────────────

    /**
     * Raw bit pattern for JNI; the C++ side re-interprets based on {@link #typeOrdinal()}.
     */
    public long rawBits() {
        return rawBits;
    }

    /**
     * C++ {@code score_type} ordinal for JNI transport.
     * Uses {@link ScoreType#cppOrdinal()} so the value matches the C++ enum,
     * not the Java enum's {@link Enum#ordinal()}.
     */
    public int typeOrdinal() {
        return type.cppOrdinal();
    }

    // ─── Standard object methods ──────────────────────────────────────────────

    @Override
    public String toString() {
        String value = switch (type) {
            case I32 -> String.valueOf((int) rawBits);
            case I64 -> String.valueOf(rawBits);
            case F32 -> String.valueOf(Float.intBitsToFloat((int) rawBits));
            case F64 -> String.valueOf(Double.longBitsToDouble(rawBits));
        };
        return "Score{" + type + ", " + value + "}";
    }
}
