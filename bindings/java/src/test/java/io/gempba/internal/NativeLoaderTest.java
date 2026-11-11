package io.gempba.internal;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

/**
 * Pure-Java tests for {@link NativeLoader}: a recording loader stands in for {@link System#load}.
 */
class NativeLoaderTest {

    @AfterEach
    void clear_native_path() {
        System.clearProperty(NativeLoader.NATIVE_PATH_PROPERTY);
    }

    @Test
    void native_path_replaces_the_bundled_library(@TempDir Path dir) throws IOException {
        Path library = Files.createFile(dir.resolve("gempba_jni_custom"));
        System.setProperty(NativeLoader.NATIVE_PATH_PROPERTY, library.toString());

        List<String> loaded = new ArrayList<>();
        NativeLoader.load("gempba_jni", loaded::add);

        assertEquals(List.of(library.toAbsolutePath().toString()), loaded);
    }

    @Test
    void native_path_naming_no_file_fails(@TempDir Path dir) {
        Path missing = dir.resolve("missing");
        System.setProperty(NativeLoader.NATIVE_PATH_PROPERTY, missing.toString());

        List<String> loaded = new ArrayList<>();
        UnsatisfiedLinkError error = assertThrows(UnsatisfiedLinkError.class,
                () -> NativeLoader.load("gempba_jni", loaded::add));

        assertTrue(loaded.isEmpty());
        assertTrue(error.getMessage().contains(NativeLoader.NATIVE_PATH_PROPERTY), error.getMessage());
        assertTrue(error.getMessage().contains(missing.toString()), error.getMessage());
    }
}
