package io.gempba.internal;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.function.Consumer;

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
 *
 * <p>The {@value #NATIVE_PATH_PROPERTY} system property names a library file to
 * load instead of the bundled one, such as a {@code gempba_jni} built against
 * another MPI than the one {@code gempba-mpi} is built for.
 */
final class NativeLoader {

    /**
     * System property naming a native library file that replaces the bundled one.
     */
    static final String NATIVE_PATH_PROPERTY = "gempba.native.path";

    private NativeLoader() {
    }

    /**
     * Loads the native library with the given base name.
     *
     * <p>Loads the file {@value #NATIVE_PATH_PROPERTY} names when it is set.
     * Otherwise tries the classpath-extracted approach first; falls back to
     * {@link System#loadLibrary} so that a locally installed library (e.g.
     * during development when the .so is on {@code LD_LIBRARY_PATH}) is still
     * picked up.
     *
     * @param libName base name without prefix or extension (e.g. {@code "gempba_jni"})
     * @throws UnsatisfiedLinkError if the library cannot be found or loaded
     */
    static void load(String libName) {
        load(libName, System::load);
    }

    /**
     * {@link #load(String)}, with {@code systemLoad} in place of {@link System#load}.
     */
    static void load(String libName, Consumer<String> systemLoad) {
        String override = System.getProperty(NATIVE_PATH_PROPERTY);
        if (override != null && !override.isBlank()) {
            loadFile(Path.of(override).toAbsolutePath(), systemLoad);
            return;
        }

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
            systemLoad.accept(tmp.toAbsolutePath().toString());
        } catch (IOException e) {
            throw new UnsatisfiedLinkError(
                    "Failed to extract native library '" + resourcePath + "': " + e.getMessage());
        }
    }

    private static void loadFile(Path path, Consumer<String> systemLoad) {
        if (!Files.isRegularFile(path)) {
            throw new UnsatisfiedLinkError(NATIVE_PATH_PROPERTY + " names " + path + ", which is not a file");
        }
        systemLoad.accept(path.toString());
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
