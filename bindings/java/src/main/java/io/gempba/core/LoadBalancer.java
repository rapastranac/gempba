package io.gempba.core;

import io.gempba.internal.GemPBANative;

/**
 * Java handle to a C++ {@code gempba::load_balancer} instance.
 *
 * <p>Instances are created through {@link io.gempba.GemPBA#createLoadBalancer} and
 * must not be constructed directly. The underlying C++ object is owned by the
 * gempba singleton and must not be destroyed while nodes are in flight.
 */
public final class LoadBalancer implements AutoCloseable {

    /**
     * Pointer to the C++ {@code load_balancer*}, cast to {@code jlong}.
     * Public so the binding's other packages (io.gempba.scheduler, the variant
     * GemPBA entry-point) can pass it through to the JNI surface; not part of
     * the user-facing API.
     */
    public final long handle;

    public LoadBalancer(long handle) {
        if (handle == 0L) {
            throw new IllegalStateException("Native load balancer creation failed (null handle)");
        }
        this.handle = handle;
    }

    /**
     * Releases the native load balancer.
     *
     * <p><strong>Note:</strong> call only after all nodes and the node manager
     * have been shut down. In typical usage {@link io.gempba.GemPBA#shutdown()} handles
     * teardown order automatically.
     */
    @Override
    public void close() {
        GemPBANative.LoadBalancer.destroy(handle);
    }
}
