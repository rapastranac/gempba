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

#include <atomic>
#include <cstdint>
#include <string>

#include <jni.h>

#include <gempba/cabi/gempba.h>

// ─── Internal helpers ─────────────────────────────────────────────────────────

namespace {

    JavaVM* g_jvm = nullptr;

    jmethodID g_node_execute_mid = nullptr;
    jmethodID g_node_lazyinit_mid = nullptr;
    jmethodID g_sr_execute_mid = nullptr;

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

    // ── User-data: holds the JNI global ref for a Java callback ──────────────

    // atomic so the early-release in one-shot callbacks can't race with the
    // tail-end release_fn (both go through exchange()).
    struct java_user_data {
        std::atomic<jobject> global_ref{nullptr};
    };

    // Releases the GlobalRef early so the Java Node's Cleaner can fire.
    // Without this the wrapper ↔ Node cycle (GlobalRef pins Node, Cleaner
    // waits on Node unreachability) leaks every node for the process lifetime.
    void release_global_ref_now(java_user_data* d) noexcept {
        if (d == nullptr)
            return;
        jobject ref = d->global_ref.exchange(nullptr, std::memory_order_acq_rel);
        if (ref == nullptr)
            return;
        if (JNIEnv* env = get_env())
            env->DeleteGlobalRef(ref);
    }

    extern "C" void java_user_data_release(void* ud) {
        auto* d = static_cast<java_user_data*>(ud);
        if (d == nullptr)
            return;
        release_global_ref_now(d); // idempotent
        delete d;
    }

    java_user_data* new_user_data(JNIEnv* env, jobject callback) {
        auto* d = new java_user_data{};
        jobject ref = env->NewGlobalRef(callback);
        if (!ref) {
            delete d;
            return nullptr;
        }
        d->global_ref.store(ref, std::memory_order_release);
        return d;
    }

    // ── Java callback bridges (extern "C", invoked by gempba core) ──────────

    gempba_status_t invoke_java_byte_callback(java_user_data* d, jmethodID mid, uint64_t thread_id, gempba_bytes_t args, jlong handle_arg, gempba_buffer_t* out_result) {
        JNIEnv* env = get_env();
        if (env == nullptr)
            return GEMPBA_ERR_RUNTIME;

        jobject ref = d->global_ref.load(std::memory_order_acquire);
        if (ref == nullptr)
            return GEMPBA_ERR_RUNTIME; // already released

        jbyteArray j_args = bytes_to_jbytearray(env, args);
        const jlong j_tid = static_cast<jlong>(thread_id);

        auto j_result = static_cast<jbyteArray>(env->CallObjectMethod(ref, mid, j_tid, j_args, handle_arg));

        if (j_args)
            env->DeleteLocalRef(j_args);

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            if (j_result)
                env->DeleteLocalRef(j_result);
            return GEMPBA_ERR_CALLBACK;
        }

        *out_result = jbytearray_to_buffer(env, j_result);
        if (j_result)
            env->DeleteLocalRef(j_result);
        return GEMPBA_OK;
    }

    extern "C" gempba_status_t java_node_runnable_callback(void* user_data, uint64_t thread_id, gempba_bytes_t args, gempba_node_t parent, gempba_buffer_t* out_result) {
        auto* d = static_cast<java_user_data*>(user_data);
        const jlong handle = (parent == nullptr) ? 0L : reinterpret_cast<jlong>(parent);
        const gempba_status_t status = invoke_java_byte_callback(d, g_node_execute_mid, thread_id, args, handle, out_result);
        release_global_ref_now(d); // node runnables are one-shot
        return status;
    }

    extern "C" gempba_status_t java_sr_runnable_callback(void* user_data, uint64_t thread_id, gempba_bytes_t args, gempba_node_t /*parent*/, gempba_buffer_t* out_result) {
        // SR is multi-shot — no early release; release_fn handles teardown.
        return invoke_java_byte_callback(static_cast<java_user_data*>(user_data), g_sr_execute_mid, thread_id, args, /*handle=*/0L, out_result);
    }

    extern "C" gempba_status_t java_node_lazy_args_callback(void* user_data, gempba_bool_t* produced, gempba_buffer_t* out_args) {
        auto* d = static_cast<java_user_data*>(user_data);
        JNIEnv* env = get_env();
        if (env == nullptr) {
            *produced = 0;
            return GEMPBA_ERR_RUNTIME;
        }

        jobject ref = d->global_ref.load(std::memory_order_acquire);
        if (ref == nullptr) {
            *produced = 0;
            return GEMPBA_ERR_RUNTIME;
        }
        auto j_result = static_cast<jbyteArray>(env->CallObjectMethod(ref, g_node_lazyinit_mid));

        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            if (j_result)
                env->DeleteLocalRef(j_result);
            *produced = 0;
            release_global_ref_now(d); // pruned: runnable_callback won't fire
            return GEMPBA_OK; // Java exception → prune, matches old shim.
        }
        if (j_result == nullptr) {
            *produced = 0;
            release_global_ref_now(d); // pruned: runnable_callback won't fire
            return GEMPBA_OK;
        }

        *produced = 1;
        *out_args = jbytearray_to_buffer(env, j_result);
        env->DeleteLocalRef(j_result);
        return GEMPBA_OK;
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

    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_24) != JNI_OK)
        return JNI_ERR;

    jclass node_cls = env->FindClass("io/gempba/core/Node");
    if (!node_cls)
        return JNI_ERR;
    g_node_execute_mid = env->GetMethodID(node_cls, "_execute", "(J[BJ)[B");
    g_node_lazyinit_mid = env->GetMethodID(node_cls, "_lazyInitArgs", "()[B");
    if (!g_node_execute_mid || !g_node_lazyinit_mid)
        return JNI_ERR;
    env->DeleteLocalRef(node_cls);

    // SerialRunnable lives in the multiprocessing variant only.  In a
    // multithreading-only JAR the class is absent from the classpath, so
    // FindClass legitimately returns NULL.  Swallow the pending
    // ClassNotFoundException and continue — the corresponding native methods
    // (gempba_serial_runnable_*) are never invoked from MT-only Java code.
    jclass sr_cls = env->FindClass("io/gempba/scheduler/SerialRunnable");
    if (sr_cls) {
        g_sr_execute_mid = env->GetMethodID(sr_cls, "_execute", "(J[BJ)[B");
        if (!g_sr_execute_mid)
            return JNI_ERR;
        env->DeleteLocalRef(sr_cls);
    } else if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }

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

    // ─── gempba globals ──────────────────────────────────────────────────────────

    JNIEXPORT jlong JNICALL JNI_FN(createDummyNode)(JNIEnv* env, jclass, jlong lb_handle) {
        auto lb = from_jlong<gempba_load_balancer_t>(lb_handle);
        gempba_node_t node = nullptr;
        if (gempba_create_dummy_node(lb, &node) != GEMPBA_OK) {
            throw_from_last_error(env, "native createDummyNode failed");
            return 0L;
        }
        return to_jlong(node);
    }

    JNIEXPORT jlong JNICALL JNI_FN(createSeedNode)(JNIEnv* env, jclass, jlong lb_handle, jobject callback, jbyteArray args_bytes) {
        auto lb = from_jlong<gempba_load_balancer_t>(lb_handle);
        auto* ud = new_user_data(env, callback);
        if (!ud)
            return 0L;

        auto args_borrow = borrow_jbytearray(env, args_bytes);
        gempba_node_t node = nullptr;
        if (gempba_create_seed_node(lb, java_node_runnable_callback, ud, java_user_data_release, args_borrow.bytes, &node) != GEMPBA_OK) {
            throw_from_last_error(env, "native createSeedNode failed");
            return 0L;
        }
        return to_jlong(node);
    }

    JNIEXPORT jlong JNICALL JNI_FN(getNodeManagerHandle)(JNIEnv* env, jclass) {
        auto nm = gempba_get_node_manager();
        if (nm == nullptr) {
            throw_from_last_error(env, "native getNodeManagerHandle failed");
            return 0L;
        }
        return to_jlong(nm);
    }

    JNIEXPORT jlong JNICALL JNI_FN(getLoadBalancerHandle)(JNIEnv*, jclass) { return to_jlong(gempba_get_load_balancer()); }

    JNIEXPORT jint JNICALL JNI_FN(shutdown)(JNIEnv*, jclass) { return gempba_shutdown(); }

    // ─── gempba::node ────────────────────────────────────────────────────────────

    JNIEXPORT void JNICALL JNI_NODE(destroy)(JNIEnv*, jclass, jlong handle) {
        if (handle == 0L)
            return;
        gempba_node_destroy(from_jlong<gempba_node_t>(handle));
    }

    JNIEXPORT jbyteArray JNICALL JNI_NODE(getResult)(JNIEnv* env, jclass, jlong handle) {
        if (handle == 0L) {
            throw_java_runtime(env, "Node.getResult: handle is 0");
            return nullptr;
        }
        gempba_buffer_t buf{nullptr, 0};
        if (gempba_node_get_result(from_jlong<gempba_node_t>(handle), &buf) != GEMPBA_OK) {
            throw_from_last_error(env, "native Node.getResult failed");
            return nullptr;
        }
        if (buf.data == nullptr)
            return nullptr;
        jbyteArray arr = bytes_to_jbytearray(env, gempba_bytes_t{buf.data, buf.len});
        gempba_buffer_free(&buf);
        return arr;
    }

    // ─── gempba::load_balancer ───────────────────────────────────────────────────

    JNIEXPORT void JNICALL JNI_LB(destroy)(JNIEnv*, jclass, jlong /*handle*/) {
        // Owned by the gempba singleton; freed via gempba_shutdown().
    }

    // ─── gempba::mt ──────────────────────────────────────────────────────────────

    JNIEXPORT jlong JNICALL JNI_MT(createLoadBalancer)(JNIEnv* env, jclass, jint policy_ordinal) {
        auto lb = gempba_mt_create_load_balancer(static_cast<gempba_balancing_policy_t>(policy_ordinal));
        if (lb == nullptr) {
            throw_from_last_error(env, "native MT.createLoadBalancer failed");
            return 0L;
        }
        return to_jlong(lb);
    }

    JNIEXPORT jlong JNICALL JNI_MT(createNodeManager)(JNIEnv* env, jclass, jlong lb_handle) {
        auto nm = gempba_mt_create_node_manager(from_jlong<gempba_load_balancer_t>(lb_handle));
        if (nm == nullptr) {
            throw_from_last_error(env, "native MT.createNodeManager failed");
            return 0L;
        }
        return to_jlong(nm);
    }

    JNIEXPORT jlong JNICALL JNI_MT(createExplicitNode)(JNIEnv* env, jclass, jlong lb_handle, jlong parent_handle, jobject callback, jbyteArray args_bytes) {
        auto* ud = new_user_data(env, callback);
        if (!ud)
            return 0L;

        auto args_borrow = borrow_jbytearray(env, args_bytes);
        gempba_node_t node = nullptr;
        if (gempba_mt_create_explicit_node(from_jlong<gempba_load_balancer_t>(lb_handle), from_jlong<gempba_node_t>(parent_handle), java_node_runnable_callback, ud, java_user_data_release,
                                           args_borrow.bytes, &node) != GEMPBA_OK) {
            throw_from_last_error(env, "native MT.createExplicitNode failed");
            return 0L;
        }
        return to_jlong(node);
    }

    JNIEXPORT jlong JNICALL JNI_MT(createLazyNode)(JNIEnv* env, jclass, jlong lb_handle, jlong parent_handle, jobject callback, jbyteArray /*unused*/) {
        auto* ud = new_user_data(env, callback);
        if (!ud)
            return 0L;

        gempba_node_t node = nullptr;
        if (gempba_mt_create_lazy_node(from_jlong<gempba_load_balancer_t>(lb_handle), from_jlong<gempba_node_t>(parent_handle), java_node_runnable_callback, java_node_lazy_args_callback, ud,
                                       java_user_data_release, &node) != GEMPBA_OK) {
            throw_from_last_error(env, "native MT.createLazyNode failed");
            return 0L;
        }
        return to_jlong(node);
    }

    // ─── gempba::mp ──────────────────────────────────────────────────────────────

    JNIEXPORT jlong JNICALL JNI_MP(createExplicitNode)(JNIEnv* env, jclass, jlong lb_handle, jlong parent_handle, jobject callback, jbyteArray args_bytes) {
        auto* ud = new_user_data(env, callback);
        if (!ud)
            return 0L;

        auto args_borrow = borrow_jbytearray(env, args_bytes);
        gempba_node_t node = nullptr;
        if (gempba_mp_create_explicit_node(from_jlong<gempba_load_balancer_t>(lb_handle), from_jlong<gempba_node_t>(parent_handle), java_node_runnable_callback, ud, java_user_data_release,
                                           args_borrow.bytes, &node) != GEMPBA_OK) {
            throw_from_last_error(env, "native MP.createExplicitNode failed");
            return 0L;
        }
        return to_jlong(node);
    }

    JNIEXPORT jlong JNICALL JNI_MP(createLoadBalancer)(JNIEnv* env, jclass, jint policy_ordinal, jlong worker_handle) {
        auto lb =
                gempba_mp_create_load_balancer(static_cast<gempba_balancing_policy_t>(policy_ordinal), worker_handle == 0L ? nullptr : from_jlong<gempba_scheduler_worker_t>(worker_handle));
        if (lb == nullptr) {
            throw_from_last_error(env, "native MP.createLoadBalancer failed");
            return 0L;
        }
        return to_jlong(lb);
    }

    JNIEXPORT jlong JNICALL JNI_MP(createNodeManager)(JNIEnv* env, jclass, jlong lb_handle, jlong worker_handle) {
        auto nm = gempba_mp_create_node_manager(from_jlong<gempba_load_balancer_t>(lb_handle), worker_handle == 0L ? nullptr : from_jlong<gempba_scheduler_worker_t>(worker_handle));
        if (nm == nullptr) {
            throw_from_last_error(env, "native MP.createNodeManager failed");
            return 0L;
        }
        return to_jlong(nm);
    }

    // ─── gempba::node_manager ────────────────────────────────────────────────────

    JNIEXPORT void JNICALL JNI_NM(setGoal)(JNIEnv*, jclass, jlong handle, jint goal_ordinal, jint score_type_ordinal) {
        gempba_nm_set_goal(from_jlong<gempba_node_manager_t>(handle), static_cast<gempba_goal_t>(goal_ordinal), static_cast<gempba_score_type_t>(score_type_ordinal));
    }

    JNIEXPORT void JNICALL JNI_NM(setBalancingPolicy)(JNIEnv*, jclass, jlong handle, jint policy_ordinal) {
        gempba_nm_set_balancing_policy(from_jlong<gempba_node_manager_t>(handle), static_cast<gempba_balancing_policy_t>(policy_ordinal));
    }

    JNIEXPORT void JNICALL JNI_NM(setThreadPoolSize)(JNIEnv*, jclass, jlong handle, jint n) {
        gempba_nm_set_thread_pool_size(from_jlong<gempba_node_manager_t>(handle), static_cast<uint32_t>(n));
    }

    JNIEXPORT jboolean JNICALL JNI_NM(tryLocalSubmit)(JNIEnv* env, jclass, jlong handle, jlong node_handle) {
        gempba_bool_t accepted = 0;
        if (gempba_nm_try_local_submit(from_jlong<gempba_node_manager_t>(handle), from_jlong<gempba_node_t>(node_handle), &accepted) != GEMPBA_OK) {
            throw_from_last_error(env, "native tryLocalSubmit failed");
            return JNI_FALSE;
        }
        return accepted ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT void JNICALL JNI_NM(forward)(JNIEnv* env, jclass, jlong handle, jlong node_handle) {
        if (gempba_nm_forward(from_jlong<gempba_node_manager_t>(handle), from_jlong<gempba_node_t>(node_handle)) != GEMPBA_OK) {
            throw_from_last_error(env, "native forward failed");
        }
    }

    JNIEXPORT jboolean JNICALL JNI_NM(tryUpdateResultMT)(JNIEnv*, jclass, jlong handle, jlong raw_bits, jint type_ordinal) {
        return gempba_nm_try_update_score(from_jlong<gempba_node_manager_t>(handle), static_cast<int64_t>(raw_bits), static_cast<gempba_score_type_t>(type_ordinal)) ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jboolean JNICALL JNI_NM(tryUpdateResult)(JNIEnv* env, jclass, jlong handle, jbyteArray result_bytes, jlong raw_bits, jint type_ordinal) {
        auto borrow = borrow_jbytearray(env, result_bytes);
        return gempba_nm_try_update_result(from_jlong<gempba_node_manager_t>(handle), borrow.bytes, static_cast<int64_t>(raw_bits), static_cast<gempba_score_type_t>(type_ordinal))
                       ? JNI_TRUE
                       : JNI_FALSE;
    }

    JNIEXPORT jlong JNICALL JNI_NM(getScoreRawBits)(JNIEnv*, jclass, jlong handle) { return static_cast<jlong>(gempba_nm_get_score_raw(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT jint JNICALL JNI_NM(getScoreTypeOrdinal)(JNIEnv*, jclass, jlong handle) { return static_cast<jint>(gempba_nm_get_score_kind(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT void JNICALL JNI_NM(setScore)(JNIEnv*, jclass, jlong handle, jlong raw_bits, jint type_ordinal) {
        gempba_nm_set_score(from_jlong<gempba_node_manager_t>(handle), static_cast<int64_t>(raw_bits), static_cast<gempba_score_type_t>(type_ordinal));
    }

    JNIEXPORT jbyteArray JNICALL JNI_NM(getResultBytes)(JNIEnv* env, jclass, jlong handle) {
        gempba_buffer_t buf{nullptr, 0};
        if (gempba_nm_get_result(from_jlong<gempba_node_manager_t>(handle), &buf) != GEMPBA_OK) {
            throw_from_last_error(env, "native getResultBytes failed");
            return nullptr;
        }
        if (buf.data == nullptr || buf.len == 0)
            return nullptr;
        jbyteArray arr = bytes_to_jbytearray(env, gempba_bytes_t{buf.data, buf.len});
        gempba_buffer_free(&buf);
        return arr;
    }

    JNIEXPORT jint JNICALL JNI_NM(generateUniqueId)(JNIEnv*, jclass, jlong handle) { return static_cast<jint>(gempba_nm_generate_unique_id(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT jdouble JNICALL JNI_NM(getWallTime)(JNIEnv*, jclass, jlong /*handle*/) { return static_cast<jdouble>(gempba_nm_wall_time()); }

    JNIEXPORT jint JNICALL JNI_NM(rankMe)(JNIEnv*, jclass, jlong handle) { return static_cast<jint>(gempba_nm_rank_me(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT jlong JNICALL JNI_NM(getThreadRequestCount)(JNIEnv*, jclass, jlong handle) {
        return static_cast<jlong>(gempba_nm_thread_request_count(from_jlong<gempba_node_manager_t>(handle)));
    }

    JNIEXPORT jdouble JNICALL JNI_NM(getIdleTime)(JNIEnv*, jclass, jlong handle) { return static_cast<jdouble>(gempba_nm_idle_time(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT jint JNICALL JNI_NM(getBalancingPolicy)(JNIEnv*, jclass, jlong handle) { return static_cast<jint>(gempba_nm_get_balancing_policy(from_jlong<gempba_node_manager_t>(handle))); }

    JNIEXPORT jboolean JNICALL JNI_NM(tryRemoteSubmit)(JNIEnv* env, jclass, jlong handle, jlong node_handle, jint runnable_id) {
        gempba_bool_t accepted = 0;
        if (gempba_nm_try_remote_submit(from_jlong<gempba_node_manager_t>(handle), from_jlong<gempba_node_t>(node_handle), static_cast<int32_t>(runnable_id), &accepted) != GEMPBA_OK) {
            throw_from_last_error(env, "native tryRemoteSubmit failed");
            return JNI_FALSE;
        }
        return accepted ? JNI_TRUE : JNI_FALSE;
    }

    JNIEXPORT jboolean JNICALL JNI_NM(isDone)(JNIEnv*, jclass, jlong handle) { return gempba_nm_is_done(from_jlong<gempba_node_manager_t>(handle)) ? JNI_TRUE : JNI_FALSE; }

    JNIEXPORT void JNICALL JNI_NM(waitForCompletion)(JNIEnv* env, jclass, jlong handle) {
        if (gempba_nm_wait(from_jlong<gempba_node_manager_t>(handle)) != GEMPBA_OK) {
            throw_from_last_error(env, "native waitForCompletion failed");
        }
    }

    JNIEXPORT void JNICALL JNI_NM(destroy)(JNIEnv*, jclass, jlong /*handle*/) {
        // Owned by the gempba singleton; freed via gempba_shutdown().
    }

    // ─── gempba::mp::serial_runnable ────────────────────────────────────────────

    JNIEXPORT jlong JNICALL JNI_SR(create)(JNIEnv* env, jclass, jint id, jobject callback, jboolean returns_value) {
        auto* ud = new_user_data(env, callback);
        if (!ud)
            return 0L;

        gempba_serial_runnable_t sr = nullptr;
        if (gempba_serial_runnable_create(static_cast<int32_t>(id), returns_value == JNI_TRUE ? 1 : 0, java_sr_runnable_callback, ud, java_user_data_release, &sr) != GEMPBA_OK) {
            throw_from_last_error(env, "native SerialRunnable.create failed");
            return 0L;
        }
        return to_jlong(sr);
    }

    JNIEXPORT void JNICALL JNI_SR(destroy)(JNIEnv*, jclass, jlong handle) {
        if (handle == 0L)
            return;
        gempba_serial_runnable_destroy(from_jlong<gempba_serial_runnable_t>(handle));
    }

    JNIEXPORT jbyteArray JNICALL JNI_SR(invoke)(JNIEnv* env, jclass, jlong sr_handle, jlong nm_handle, jbyteArray args_bytes) {
        auto borrow = borrow_jbytearray(env, args_bytes);
        gempba_buffer_t out{nullptr, 0};
        if (gempba_serial_runnable_invoke(from_jlong<gempba_serial_runnable_t>(sr_handle), from_jlong<gempba_node_manager_t>(nm_handle), borrow.bytes, &out) != GEMPBA_OK) {
            throw_from_last_error(env, "native SerialRunnable.invoke failed");
            return nullptr;
        }
        if (out.data == nullptr || out.len == 0)
            return nullptr;
        jbyteArray arr = bytes_to_jbytearray(env, gempba_bytes_t{out.data, out.len});
        gempba_buffer_free(&out);
        return arr;
    }

    // ─── gempba::telemetry (process-wide flag) ───────────────────────────────────

    JNIEXPORT void JNICALL JNI_TELEM(enable)(JNIEnv*, jclass) { gempba_telemetry_enable(); }

    JNIEXPORT void JNICALL JNI_TELEM(disable)(JNIEnv*, jclass) { gempba_telemetry_disable(); }

    JNIEXPORT jboolean JNICALL JNI_TELEM(isEnabled)(JNIEnv*, jclass) { return gempba_telemetry_is_enabled() ? JNI_TRUE : JNI_FALSE; }

} // extern "C"
