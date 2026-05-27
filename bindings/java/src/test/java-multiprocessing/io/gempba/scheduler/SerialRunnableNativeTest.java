package io.gempba.scheduler;

import io.gempba.core.LoadBalancer;
import io.gempba.core.NodeManager;
import io.gempba.value.BalancingPolicy;
import io.gempba.value.Goal;
import io.gempba.value.Score;
import io.gempba.value.ScoreType;

import io.gempba.internal.GemPBANative;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.Test;

import java.nio.ByteBuffer;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.jupiter.api.Assertions.*;
import static org.junit.jupiter.api.Assumptions.assumeTrue;

/**
 * JNI-level tests for {@code gempba::mp::serial_runnable} bindings.
 *
 * <p>Both flavours are exercised:
 * <ul>
 *   <li><strong>Void</strong> ({@code returnsValue = false}): the Java callback
 *       runs on a thread-pool thread; the caller does not receive a result.</li>
 *   <li><strong>Non-void</strong> ({@code returnsValue = true}): the Java callback
 *       returns serialised bytes that are forwarded back to the caller.</li>
 * </ul>
 *
 * <p>Each test creates its own load balancer and node manager via
 * {@link GemPBANative.MT} and tears down with {@link GemPBANative#shutdown()}.
 */
class SerialRunnableNativeTest {

    /**
     * Convenience: 4 bytes encoding the integer 42.
     */
    private static final byte[] BYTES_42 = ByteBuffer.allocate(4).putInt(42).array();

    /**
     * The multiprocessing JAR can be loaded against an MT-flavored
     * {@code gempba_jni} (built with {@code GEMPBA_MULTIPROCESSING=OFF}) if a
     * developer rebuilt the native for one variant and then ran tests under
     * the other.  In that case every {@code gempba_serial_runnable_*} call
     * resolves to a stub that throws {@code RuntimeException}.  Probe once
     * here so the whole class skips with a clear message instead of failing
     * each method with the same noise.
     *
     * <p>The probe uses an unregistered placeholder {@link SerialRunnable}
     * because the JNI {@code create} shim short-circuits to {@code 0L} on a
     * null callback (it can't take a global ref), so a null probe would
     * never reach the C ABI stub at all.
     */
    @BeforeAll
    static void requireMultiprocessingNative() {
        SerialRunnable probe = new SerialRunnable(-1, 0L, (tid, args) -> null);
        long handle = 0L;
        try {
            handle = GemPBANative.SerialRunnable.create(-1, probe, false);
        } catch (RuntimeException e) {
            String msg = e.getMessage();
            assumeTrue(
                    msg == null || !msg.contains("GEMPBA_MULTIPROCESSING"),
                    "Native gempba_jni was built without GEMPBA_MULTIPROCESSING; " +
                            "rebuild it via scripts/build_jar_<os>.sh MP (or pass " +
                            "-DGEMPBA_MULTIPROCESSING=ON to cmake) to run these tests.");
            throw e; // some other RuntimeException — re-raise
        } finally {
            if (handle != 0L) GemPBANative.SerialRunnable.destroy(handle);
            GemPBANative.shutdown();
        }
    }

    @AfterEach
    void tearDown() {
        GemPBANative.shutdown();
    }

    // ─── Handle creation and destruction ─────────────────────────────────────

    @Test
    void create_void_runnable_returns_valid_handle() {
        SerialRunnable sr = makeVoidRunnable(1, (tid, args) -> null);
        assertNotEquals(0L, sr.handle, "create(returnsValue=false) must return a non-zero handle");
        sr.close();
    }

    @Test
    void create_nonvoid_runnable_returns_valid_handle() {
        SerialRunnable sr = makeNonVoidRunnable(2, (tid, args) -> args); // echo
        assertNotEquals(0L, sr.handle, "create(returnsValue=true) must return a non-zero handle");
        sr.close();
    }

    @Test
    void destroy_with_zero_handle_is_safe() {
        GemPBANative.SerialRunnable.destroy(0L);   // must not crash
    }

    // ─── Void runnable execution ──────────────────────────────────────────────

    @Test
    void void_runnable_callback_is_invoked() throws InterruptedException {
        CountDownLatch latch = new CountDownLatch(1);

        SerialRunnable sr = makeVoidRunnable(10, (tid, args) -> {
            latch.countDown();
            return null;
        });

        long nmHandle = createNM();
        GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertTrue(latch.await(5, TimeUnit.SECONDS),
                "void callback must be invoked within 5 s");

        sr.close();
    }

    @Test
    void void_runnable_receives_correct_thread_id() throws InterruptedException {
        AtomicLong capturedTid = new AtomicLong(-1L);
        CountDownLatch latch = new CountDownLatch(1);

        SerialRunnable sr = makeVoidRunnable(11, (tid, args) -> {
            capturedTid.set(tid);
            latch.countDown();
            return null;
        });

        long nmHandle = createNM();
        GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertTrue(latch.await(5, TimeUnit.SECONDS));
        assertNotEquals(-1L, capturedTid.get(),
                "thread-id passed to void callback must be set by the framework");
        sr.close();
    }

    @Test
    void void_runnable_receives_correct_args() throws InterruptedException {
        AtomicReference<byte[]> captured = new AtomicReference<>();
        CountDownLatch latch = new CountDownLatch(1);

        SerialRunnable sr = makeVoidRunnable(12, (tid, args) -> {
            captured.set(args);
            latch.countDown();
            return null;
        });

        long nmHandle = createNM();
        GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertTrue(latch.await(5, TimeUnit.SECONDS));
        assertNotNull(captured.get());
        assertEquals(42, ByteBuffer.wrap(captured.get()).getInt(),
                "args bytes must arrive unchanged at the void callback");
        sr.close();
    }

    // ─── Non-void runnable execution ─────────────────────────────────────────

    @Test
    void nonvoid_runnable_returns_null_when_callback_returns_null() {
        SerialRunnable sr = makeNonVoidRunnable(20, (tid, args) -> null);

        long nmHandle = createNM();
        byte[] result = GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        // Null result bytes → empty task_packet → null jbyteArray back
        assertNull(result, "null from callback must propagate as null jbyteArray");
        sr.close();
    }

    @Test
    void nonvoid_runnable_returns_result_bytes() {
        // Echo the args back: result == input
        SerialRunnable sr = makeNonVoidRunnable(21, (tid, args) -> args);

        long nmHandle = createNM();
        byte[] result = GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertNotNull(result);
        assertEquals(42, ByteBuffer.wrap(result).getInt(),
                "non-void callback return value must be forwarded as result bytes");
        sr.close();
    }

    @Test
    void nonvoid_runnable_can_transform_args() {
        // Double the input integer and return it
        SerialRunnable sr = makeNonVoidRunnable(22, (tid, args) -> {
            int value = ByteBuffer.wrap(args).getInt();
            return ByteBuffer.allocate(4).putInt(value * 2).array();
        });

        long nmHandle = createNM();
        byte[] result = GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertNotNull(result);
        assertEquals(84, ByteBuffer.wrap(result).getInt(),
                "non-void callback must be able to transform args (42 * 2 = 84)");
        sr.close();
    }

    @Test
    void nonvoid_runnable_receives_correct_thread_id() {
        AtomicLong capturedTid = new AtomicLong(-1L);

        SerialRunnable sr = makeNonVoidRunnable(23, (tid, args) -> {
            capturedTid.set(tid);
            return args;
        });

        long nmHandle = createNM();
        GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertNotEquals(-1L, capturedTid.get(),
                "thread-id passed to non-void callback must be set by the framework");
        sr.close();
    }

    @Test
    void nonvoid_runnable_receives_correct_args() {
        AtomicReference<byte[]> captured = new AtomicReference<>();

        SerialRunnable sr = makeNonVoidRunnable(24, (tid, args) -> {
            captured.set(args);
            return args;
        });

        long nmHandle = createNM();
        GemPBANative.SerialRunnable.invoke(sr.handle, nmHandle, BYTES_42);

        assertNotNull(captured.get());
        assertEquals(42, ByteBuffer.wrap(captured.get()).getInt(),
                "args bytes must arrive unchanged at the non-void callback");
        sr.close();
    }

    // ─── Helpers ─────────────────────────────────────────────────────────────

    private static long createNM() {
        long lbHandle = GemPBANative.MT.createLoadBalancer(BalancingPolicy.QUASI_HORIZONTAL.ordinal());
        long nmHandle = GemPBANative.MT.createNodeManager(lbHandle);
        NodeManager nm = new NodeManager(nmHandle);
        nm.setGoal(Goal.MAXIMISE, ScoreType.I32);
        nm.setThreadPoolSize(2);
        nm.setScore(Score.make(0));
        return nmHandle;
    }

    private static SerialRunnable makeVoidRunnable(int id, SerialTask task) {
        SerialRunnable sr = new SerialRunnable(id, 0L, task);
        sr.handle = GemPBANative.SerialRunnable.create(id, sr, false);
        return sr;
    }

    private static SerialRunnable makeNonVoidRunnable(int id, SerialTask task) {
        SerialRunnable sr = new SerialRunnable(id, 0L, task);
        sr.handle = GemPBANative.SerialRunnable.create(id, sr, true);
        return sr;
    }
}
