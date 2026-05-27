package io.gempba.core;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.assertDoesNotThrow;
import static org.junit.jupiter.api.Assertions.assertNotNull;

/**
 * Tests for the {@code gempba::node} JNI binding.
 *
 * <p>These tests verify the JNI plumbing layer in isolation:
 * library loading and the null-handle safety guard in {@code Node::destroy}.
 * Node creation tests live in {@link LoadBalancerTest} and
 * {@link NodeManagerTest} where a live load balancer is available.
 */
class NodeTest {

    @Test
    void native_library_loads_successfully() {
        // GemPBANative.LOADED is set in the static initialiser that calls
        // NativeLoader.load("gempba_jni").  If the library is missing or
        // symbols are unresolved the static block throws before we get here.
        assertNotNull(GemPBANative.LOADED, "Native library must be loaded before any JNI call");
    }

    @Test
    void destroy_with_zero_handle_is_safe() {
        // The C++ side guards against null: if (handle == 0L) return;
        // Calling it from Java must not crash the JVM.
        assertDoesNotThrow(() -> GemPBANative.Node.destroy(0L), "Node.destroy(0) must be a safe no-op");
    }
}
