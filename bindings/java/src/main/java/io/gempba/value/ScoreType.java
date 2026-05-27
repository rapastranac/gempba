package io.gempba.value;

/**
 * Numeric representation for a {@link Score}, mirroring the supported subset of
 * {@code gempba::score_type}.
 *
 * <p>Java exposes only the four types it can represent natively. The C++ library
 * also defines {@code U_I32} (ordinal 1), {@code U_I64} (ordinal 3), and
 * {@code F128} (ordinal 6); users who need those should work directly in C++.
 *
 * <p>Because the unsupported types are stripped, the Java {@link #ordinal()}
 * values do <em>not</em> match the C++ {@code score_type} enum. Use
 * {@link #cppOrdinal()} for all JNI transport and {@link #fromCppOrdinal(int)}
 * to reconstruct a {@code ScoreType} from a value received from native code.
 */
public enum ScoreType {
    I32(0),   // gempba::score_type::I32
    I64(2),   // gempba::score_type::I64
    F32(4),   // gempba::score_type::F32
    F64(5);   // gempba::score_type::F64

    /**
     * The ordinal that the C++ {@code score_type} enum assigns to this variant.
     */
    private final int cppOrdinal;

    ScoreType(int cppOrdinal) {
        this.cppOrdinal = cppOrdinal;
    }

    /**
     * Returns the C++ {@code score_type} ordinal for JNI transport.
     *
     * <p>Always use this instead of {@link #ordinal()} when passing a
     * {@code ScoreType} across the JNI boundary.
     */
    public int cppOrdinal() {
        return cppOrdinal;
    }

    /**
     * Reconstructs a {@code ScoreType} from a C++ {@code score_type} ordinal
     * received from native code.
     *
     * @param cppOrdinal the raw {@code score_type} ordinal from JNI
     * @return the matching {@code ScoreType}
     * @throws IllegalArgumentException if the ordinal is unsupported in Java
     */
    public static ScoreType fromCppOrdinal(int cppOrdinal) {
        return switch (cppOrdinal) {
            case 0 -> I32;
            case 2 -> I64;
            case 4 -> F32;
            case 5 -> F64;
            default -> throw new IllegalArgumentException(
                    "C++ score_type ordinal " + cppOrdinal + " is not supported in the Java binding");
        };
    }
}
