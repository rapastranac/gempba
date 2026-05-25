/*
 * MIT License
 *
 * Copyright (c) 2026. Andrés Pastrana
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * JNI shim for the gempba Java binding.
 *
 * Design
 * ──────
 * This file contains NO C++ gempba headers.  All gempba interaction goes
 * through the C ABI declared in <gempba/cabi/gempba.h>; template instantiation,
 * exception handling, and node-lifetime management live entirely in
 * src/cabi/gempba.cpp.  Adding a new language binding therefore reuses the same
 * surface that this JNI consumes.
 *
 * Handles
 * ───────
 * Every "handle" passed to/from Java is a pointer reinterpret_cast'd to jlong.
 * The pointer's underlying type is one of the opaque gempba_*_t typedefs.
 *
 * JNI symbol naming
 * ─────────────────
 * GemPBANative uses nested static inner classes that mirror the gempba
 * namespace / class hierarchy.  Each '$' in the binary class name maps to
 * '_00024' in the JNI-mangled function name.
 */

#include <cstdint>
#include <string>

#include <jni.h>

#include <gempba/cabi/gempba.h>

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace {

    JavaVM* g_jvm = nullptr;

    JNIEnv* get_env() {
        JNIEnv* env = nullptr;
        const jint rc = g_jvm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_24);
        if (rc == JNI_EDETACHED) {
            JavaVMAttachArgs args{JNI_VERSION_24, const_cast<char*>("gempba-worker"), nullptr};
            g_jvm->AttachCurrentThreadAsDaemon(reinterpret_cast<void**>(&env), &args);
        }
        return env;
    }

    // ── Bytes <-> jbyteArray ─────────────────────────────────────────────────

    /* nullptr return for empty bytes — matches existing Java contract. */
    jbyteArray bytes_to_jbytearray(JNIEnv* env, gempba_bytes_t bytes) {
        if (bytes.len == 0 || bytes.data == nullptr)
            return nullptr;
        const auto sz = static_cast<jsize>(bytes.len);
        jbyteArray arr = env->NewByteArray(sz);
        env->SetByteArrayRegion(arr, 0, sz, reinterpret_cast<const jbyte*>(bytes.data));
        return arr;
    }

    /* Copies jbyteArray into an owned C ABI buffer. */
    gempba_buffer_t jbytearray_to_buffer(JNIEnv* env, jbyteArray arr) {
        if (arr == nullptr)
            return gempba_buffer_t{nullptr, 0};
        const jsize len = env->GetArrayLength(arr);
        if (len == 0)
            return gempba_buffer_t{nullptr, 0};
        gempba_buffer_t buf = gempba_buffer_alloc(static_cast<size_t>(len));
        if (buf.data == nullptr)
            return buf;
        env->GetByteArrayRegion(arr, 0, len, reinterpret_cast<jbyte*>(buf.data));
        return buf;
    }

    /* RAII borrow of a jbyteArray's contents — avoids a copy when the C ABI
     * function only reads the bytes (not retained beyond the call). */
    struct borrowed_jbytes {
        JNIEnv* env = nullptr;
        jbyteArray arr = nullptr;
        jbyte* elems = nullptr;
        gempba_bytes_t bytes{nullptr, 0};

        ~borrowed_jbytes() {
            if (elems)
                env->ReleaseByteArrayElements(arr, elems, JNI_ABORT);
        }
    };

    borrowed_jbytes borrow_jbytearray(JNIEnv* env, jbyteArray arr) {
        borrowed_jbytes b;
        b.env = env;
        b.arr = arr;
        if (arr == nullptr)
            return b;
        b.elems = env->GetByteArrayElements(arr, nullptr);
        if (b.elems == nullptr)
            return b;
        const jsize len = env->GetArrayLength(arr);
        b.bytes.data = reinterpret_cast<const std::uint8_t*>(b.elems);
        b.bytes.len = static_cast<size_t>(len);
        return b;
    }

    // ── Java exception helpers ────────────────────────────────────────────────

    void throw_java_runtime(JNIEnv* env, const char* msg) {
        if (env->ExceptionCheck())
            return;
        jclass ex = env->FindClass("java/lang/RuntimeException");
        if (ex)
            env->ThrowNew(ex, msg ? msg : "(null)");
    }

    void throw_from_last_error(JNIEnv* env, const char* fallback) {
        const char* msg = gempba_last_error_message();
        throw_java_runtime(env, msg ? msg : fallback);
    }

    // ── Handle conversion ────────────────────────────────────────────────────

    template<typename T>
    T from_jlong(jlong h) {
        return reinterpret_cast<T>(h);
    }
    template<typename T>
    jlong to_jlong(T p) {
        return reinterpret_cast<jlong>(p);
    }

} // namespace

// ─── JNI_OnLoad ───────────────────────────────────────────────────────────────

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    g_jvm = vm;
    return JNI_VERSION_24;
}

// ─── JNI symbol name macros ──────────────────────────────────────────────────

#define _JNI_BASE Java_io_gempba_internal_GemPBANative
#define _CAT(a, b) a##b
#define CAT(a, b) _CAT(a, b)
#define JNI_FN(n) CAT(_JNI_BASE, _##n)
#define JNI_LB(n) CAT(_JNI_BASE, _00024LoadBalancer_##n)
#define JNI_NM(n) CAT(_JNI_BASE, _00024NodeManager_##n)
#define JNI_NODE(n) CAT(_JNI_BASE, _00024Node_##n)
#define JNI_MT(n) CAT(_JNI_BASE, _00024MT_##n)
#define JNI_SR(n) CAT(_JNI_BASE, _00024SerialRunnable_##n)
#define JNI_MP(n) CAT(_JNI_BASE, _00024MP_##n)
#define JNI_SCHED(n) CAT(_JNI_BASE, _00024Scheduler_##n)
#define JNI_CENTER(n) CAT(_JNI_BASE, _00024Scheduler_00024Center_##n)
#define JNI_WORKER(n) CAT(_JNI_BASE, _00024Scheduler_00024Worker_##n)
#define JNI_TELEM(n) CAT(_JNI_BASE, _00024Telemetry_##n)

extern "C" {

    // ─── gempba::telemetry (process-wide flag) ───────────────────────────────────

    JNIEXPORT void JNICALL JNI_TELEM(enable)(JNIEnv*, jclass) { gempba_telemetry_enable(); }

    JNIEXPORT void JNICALL JNI_TELEM(disable)(JNIEnv*, jclass) { gempba_telemetry_disable(); }

    JNIEXPORT jboolean JNICALL JNI_TELEM(isEnabled)(JNIEnv*, jclass) { return gempba_telemetry_is_enabled() ? JNI_TRUE : JNI_FALSE; }

} // extern "C"
