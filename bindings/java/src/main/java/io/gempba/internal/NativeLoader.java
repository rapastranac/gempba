package io.gempba.internal;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * Extracts and loads a platform-native shared library bundled inside the JAR.
 *
 * <h2>Resource layout inside the JAR</h2>
 * <pre>
 * natives/
 *   linux-x86_64/   gempba_jni.so
 *   linux-aarch64/  gempba_jni.so
 *   win-x86_64/     gempba_jni.dll
 *   osx-x86_64/     gempba_jni.dylib
 *   osx-aarch64/    gempba_jni.dylib
 * </pre>
 * The "lib" prefix System.mapLibraryName would normally add on Linux/macOS
 * is stripped to match the CMake target's {@code PREFIX ""} setting.
 *
 * <p>On the first {@link #load} call the appropriate binary is copied to a
 * temporary file and loaded via {@link System#load}. Subsequent calls for the
 * same library name are no-ops (the JVM caches loaded libraries by name).
 */
final class NativeLoader {

    private NativeLoader() {
    }

    /**
     * Loads the native library with the given base name.
     *
     * <p>Tries the classpath-extracted approach first; falls back to
     * {@link System#loadLibrary} so that a locally installed library (e.g.
     * during development when the .so is on {@code LD_LIBRARY_PATH}) is still
     * picked up.
     *
     * @param libName base name without prefix or extension (e.g. {@code "gempba_jni"})
     * @throws UnsatisfiedLinkError if the library cannot be found or loaded
     */
    static void load(String libName) {
        String classifier = classifier();
        // CMake sets PREFIX "" on the JNI target so the artifact is e.g.
        // "gempba_jni.so" rather than "libgempba_jni.so".  System.mapLibraryName
        // would re-add the "lib" prefix on Linux/macOS, so strip it before
        // looking up the resource.
        String filename = stripLibPrefix(System.mapLibraryName(libName));
        String resourcePath = "/natives/" + classifier + "/" + filename;

        try (InputStream in = NativeLoader.class.getResourceAsStream(resourcePath)) {
            if (in == null) {
                // Not bundled — try the system library path (dev / CI environments)
                System.loadLibrary(libName);
                return;
            }
            Path tmp = Files.createTempFile("gempba_", "_" + filename);
            tmp.toFile().deleteOnExit();
            try (OutputStream out = Files.newOutputStream(tmp)) {
                in.transferTo(out);
            }
            System.load(tmp.toAbsolutePath().toString());
        } catch (IOException e) {
            throw new UnsatisfiedLinkError(
                    "Failed to extract native library '" + resourcePath + "': " + e.getMessage());
        }
    }

    // ─── Platform detection ───────────────────────────────────────────────────

    private static String classifier() {
        String os = normaliseOs(System.getProperty("os.name", "").toLowerCase());
        String arch = normaliseArch(System.getProperty("os.arch", "").toLowerCase());
        return os + "-" + arch;
    }

    private static String normaliseOs(String raw) {
        if (raw.contains("linux")) return "linux";
        if (raw.contains("win")) return "win";
        if (raw.contains("mac") || raw.contains("darwin")) return "osx";
        throw new UnsatisfiedLinkError("Unsupported OS: " + raw);
    }

    private static String normaliseArch(String raw) {
        if (raw.equals("amd64") || raw.equals("x86_64")) return "x86_64";
        if (raw.equals("aarch64") || raw.equals("arm64")) return "aarch64";
        throw new UnsatisfiedLinkError("Unsupported architecture: " + raw);
    }

    private static String stripLibPrefix(String mappedName) {
        return mappedName.startsWith("lib") ? mappedName.substring(3) : mappedName;
    }
}
