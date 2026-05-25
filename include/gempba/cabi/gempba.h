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
 */

/**
 * gempba C ABI — stable, language-neutral binding surface.
 *
 * This header is the contract between the C++ gempba core and every external binding (Java/JNI, Python, Rust, …).
 * It exposes:
 *
 *   - Opaque handle types (forward-declared structs).
 *   - Plain C functions with extern "C" linkage.
 *   - Length-prefixed byte buffers for generic data.
 *   - Function-pointer callbacks with a void* user_data slot.
 *   - A thread-local last-error message for diagnostics.
 *
 * No C++ types, STL, exceptions, or templates appear in any signature.  The implementation in src/c_api.cpp owns all
 * template instantiation and exception-to-status translation.
 *
 * Each declaration cross-references its underlying C++ symbol (gempba::xxx) so binding authors can drill into the
 * canonical contract; this header intentionally describes only what differs at the ABI boundary — ownership, error
 * mapping, blocking, threading.  The reference shim is jni/src/gempba_jni.cpp.
 */

#ifndef GEMPBA_CABI_GEMPBA_H
#define GEMPBA_CABI_GEMPBA_H

/** Use the C++ <c*> wrappers when compiled as C++ so modernize-deprecated-headers stays quiet, fall back to the C-style
 * headers for C consumers (Python ctypes, Rust bindgen, plain-C bindings) — neither <cstddef> nor <cstdint> exist in C.
 */
#ifdef __cplusplus
    #include <cstddef>
    #include <cstdint>
#else
    #include <stddef.h>
    #include <stdint.h>
#endif

// The C ABI deliberately uses bare C-style identifiers (no `m_` / `p_` / `v_` Hungarian prefixes) because these names
// are part of the public contract that bindings (Java, Python, Rust, ...) wrap and surface to their consumers.
// Suppress the project's naming rule for this file.
// NOLINTBEGIN(readability-identifier-naming)

#ifdef __cplusplus
extern "C" {
#endif

    /* ─── Status & error model ──────────────────────────────────────────────── */

    /**
     * Coarse-grained status code returned from every fallible entry point.  The exact reason is recorded in the
     * thread-local last-error string; bindings that need richer diagnostics should pair the status check with a call
     * to gempba_last_error_message.
     *
     *   GEMPBA_OK                : success.
     *   GEMPBA_ERR_INVALID_ARG   : null out-pointer, null required handle, etc.
     *   GEMPBA_ERR_OUT_OF_MEMORY : allocation failure inside the core or ABI.
     *   GEMPBA_ERR_RUNTIME       : C++ std::exception caught at the boundary.
     *   GEMPBA_ERR_CALLBACK      : reserved for callback-originated failures.
     *   GEMPBA_ERR_UNKNOWN       : unknown C++ exception caught at the boundary.
     */
    typedef enum {
        GEMPBA_OK                = 0,
        GEMPBA_ERR_INVALID_ARG   = 1,
        GEMPBA_ERR_OUT_OF_MEMORY = 2,
        GEMPBA_ERR_RUNTIME       = 3,
        GEMPBA_ERR_CALLBACK      = 4,
        GEMPBA_ERR_UNKNOWN       = 99
    } gempba_status_t;

    /**
     * Returns a UTF-8 message describing the last error on the calling thread, or NULL if no error has been recorded.
     * The pointer remains valid until the next gempba call on the same thread.
     */
    const char* gempba_last_error_message(void);

    /** Resets the thread-local last-error string. */
    void gempba_clear_last_error(void);

    /* ─── Primitive types ───────────────────────────────────────────────────── */

    /**
     * Boolean carried across the ABI.  Always 0 = false / 1 = true.  Defined as a fixed-width int so signature widths
     * are stable across platforms and trivial to map from any binding language.
     */
    typedef int32_t gempba_bool_t;

    /* ─── Bytes ─────────────────────────────────────────────────────────────── */

    /** Borrowed view of a byte range.  Not retained beyond the call unless the specific function documents otherwise.
     * data may be NULL iff len == 0. */
    typedef struct {
        const uint8_t* data;
        size_t         len;
    } gempba_bytes_t;

    /** Owned buffer.  Whoever holds it MUST release it with gempba_buffer_free.  data may be NULL iff len == 0 (no
     * allocation). */
    typedef struct {
        uint8_t* data;
        size_t   len;
    } gempba_buffer_t;

    /**
     * Allocates an owned buffer of `len` bytes.  Bindings use this when populating an out_result inside a callback,
     * or any time they hand a buffer back to the gempba core.  Returns {NULL, 0} on allocation failure or when
     * len == 0.
     *
     * The underlying allocator is an implementation detail; do not pair this with free() / delete[].  Always release
     * with gempba_buffer_free.
     */
    gempba_buffer_t gempba_buffer_alloc(size_t len);

    /** Releases a buffer previously returned by gempba_buffer_alloc or by any gempba_* function that documents its out
     * parameter as owned.  After the call, *buf is reset to {NULL, 0}.  Safe to call on {NULL, 0}. */
    void gempba_buffer_free(gempba_buffer_t* buf);

    /* ─── Enums (mirror gempba C++ ordinals; keep in sync) ──────────────────── */

    /** Mirrors gempba::balancing_policy. */
    typedef enum {
        GEMPBA_BALANCING_QUASI_HORIZONTAL = 0,
        GEMPBA_BALANCING_WORK_STEALING    = 1
    } gempba_balancing_policy_t;

    /** Mirrors gempba::goal: which direction node_manager should optimise. */
    typedef enum {
        GEMPBA_GOAL_MAXIMISE = 0,
        GEMPBA_GOAL_MINIMISE = 1
    } gempba_goal_t;

    /**
     * Score storage kinds.  Mirrors gempba::score_type minus F128.
     *
     * The C++ core also defines F128 (long double / __float128).  It is omitted here because (a) none of the current
     * target bindings — Java, Python, Rust stable — have a portable 128-bit float type, and (b) the raw-bits transport
     * below is int64_t and physically cannot carry 128 bits.  Add a wider raw-bits path before re-exposing F128.
     */
    typedef enum {
        GEMPBA_SCORE_I32   = 0,
        GEMPBA_SCORE_U_I32 = 1,
        GEMPBA_SCORE_I64   = 2,
        GEMPBA_SCORE_U_I64 = 3,
        GEMPBA_SCORE_F32   = 4,
        GEMPBA_SCORE_F64   = 5
    } gempba_score_type_t;

    /** Mirrors gempba::mp::scheduler_topology. */
    typedef enum {
        GEMPBA_TOPOLOGY_SEMI_CENTRALIZED = 0,
        GEMPBA_TOPOLOGY_CENTRALIZED      = 1
    } gempba_scheduler_topology_t;

    /* ─── Opaque handles ────────────────────────────────────────────────────── */

    typedef struct gempba_load_balancer_s*    gempba_load_balancer_t;    /* gempba::load_balancer*           */
    typedef struct gempba_node_manager_s*     gempba_node_manager_t;     /* gempba::node_manager*            */
    typedef struct gempba_node_s*             gempba_node_t;             /* wraps gempba::node               */
    typedef struct gempba_serial_runnable_s*  gempba_serial_runnable_t;  /* wraps gempba::serial_runnable    */
    typedef struct gempba_scheduler_s*        gempba_scheduler_t;        /* wraps gempba::scheduler          */
    typedef struct gempba_scheduler_center_s* gempba_scheduler_center_t; /* gempba::scheduler::center*       */
    typedef struct gempba_scheduler_worker_s* gempba_scheduler_worker_t; /* gempba::scheduler::worker*       */

    /**
     * Ownership summary
     * ─────────────────
     *   gempba_node_t            : caller-owned. Free with gempba_node_destroy.
     *   gempba_serial_runnable_t : caller-owned. Free with gempba_serial_runnable_destroy.
     *   gempba_scheduler_t       : caller-owned. Free with gempba_scheduler_destroy.
     *   gempba_load_balancer_t   : singleton-owned. DO NOT free.  Released by gempba_shutdown.
     *   gempba_node_manager_t    : singleton-owned. DO NOT free.  Released by gempba_shutdown.
     *   gempba_scheduler_center_t / _worker_t : borrowed view into a scheduler. DO NOT free.
     *
     * NULL is the canonical "absent / invalid" value for every handle.
     */

    /* ─── Callbacks ─────────────────────────────────────────────────────────── */

    /**
     * User-supplied runnable invoked by the gempba core.
     *
     *   user_data       : the pointer passed at registration (binding-defined).
     *   thread_id       : opaque hash of the executing C++ thread::id.
     *   args            : argument bytes (borrowed; valid only for this call).
     *   parent          : the *executing* node, to be used as the parent when the callback spawns child nodes during
     *                     this invocation.  The gempba core passes NULL when the invocation is delegated (e.g.
     *                     m_invoke(true)); a NULL parent means "do not attach children to a tree — start a fresh
     *                     dummy".
     *   out_result      : on success, populate with an owned buffer; the gempba core takes ownership and frees it via
     *                     gempba_buffer_free.  For void runnables (no result expected), set {data=NULL, len=0}.
     *
     * Return GEMPBA_OK on success or any error code; on error the gempba core will propagate it as a runtime failure.
     * The implementation MUST NOT throw across the boundary.
     */
    typedef gempba_status_t (*gempba_runnable_fn)(void* user_data, uint64_t thread_id, gempba_bytes_t args,
                                                  gempba_node_t parent, gempba_buffer_t* out_result);

    /**
     * Lazy args initializer.  Returns the args for a freshly materialised lazy node, or signals "prune this branch".
     *
     *   out_args : if *produced == 1, populate with an owned buffer (may be empty).
     *
     * Return values:
     *   GEMPBA_OK with *produced = 1  → use *out_args
     *   GEMPBA_OK with *produced = 0  → prune; out_args is ignored
     *   any error                     → propagated as runtime failure
     */
    typedef gempba_status_t (*gempba_lazy_args_fn)(void* user_data, gempba_bool_t* produced, gempba_buffer_t* out_args);

    /**
     * Optional release hook for user_data.  Called exactly once when the gempba core drops the runnable reference.
     * Pass NULL if no cleanup is required.
     *
     * Ownership rule: every create_* / *_create function that accepts user_data + release_fn takes ownership of
     * user_data the moment it is called and guarantees release_fn fires exactly once — on success when the runnable
     * is eventually dropped, on failure before the function returns.  Bindings must NOT release user_data themselves
     * after invoking such a function, regardless of the returned status.
     */
    typedef void (*gempba_release_fn)(void* user_data);

    /* ─── Globals ───────────────────────────────────────────────────────────── */

    /**
     * Returns the process-wide load balancer, or NULL if none has been created yet.  Wraps gempba::get_load_balancer.
     * Does not transfer ownership.
     */
    gempba_load_balancer_t gempba_get_load_balancer(void);

    /**
     * Returns the process-wide node manager.  Wraps gempba::get_node_manager.  Returns NULL and sets last-error if
     * the node manager has not been created yet (the C++ accessor throws in that case).
     */
    gempba_node_manager_t gempba_get_node_manager(void);

    /**
     * Tears down every gempba singleton (load balancer, node manager, scheduler).  Wraps gempba::shutdown and forwards
     * its return code.  Returns -1 on exception with the message recorded in last-error.
     */
    int32_t gempba_shutdown(void);

    /* ─── Node creation (engine-agnostic) ───────────────────────────────────── */

    /**
     * Creates a placeholder node with no runnable, suitable as a stand-in parent when the gempba core hands the
     * runnable a NULL parent (delegated execution).  Wraps gempba::create_dummy_node.
     */
    gempba_status_t gempba_create_dummy_node(gempba_load_balancer_t lb, gempba_node_t* out);

    /**
     * Creates the root of a search tree.  Wraps gempba::create_seed_node.
     *
     * user_data + release_user_data follow the runnable-ownership rule documented on gempba_release_fn — release fires
     * exactly once, success or failure.
     */
    gempba_status_t gempba_create_seed_node(gempba_load_balancer_t lb, gempba_runnable_fn runnable, void* user_data,
                                            gempba_release_fn release_user_data, /* nullable */
                                            gempba_bytes_t args, gempba_node_t* out);

    /** Releases the C ABI wrapper.  The underlying gempba::node refcount is decremented; the runnable's user_data
     * release_fn fires when the last std::function copy is dropped, not necessarily here. */
    void gempba_node_destroy(gempba_node_t node);

    /* ─── Multithreaded engine (mt) ─────────────────────────────────────────── */

    /**
     * Installs the process-wide MT load balancer with the given policy.  Wraps gempba::mt::create_load_balancer.
     * Returns NULL with last-error set if a load balancer already exists (the C++ factory throws in that case).
     */
    gempba_load_balancer_t gempba_mt_create_load_balancer(gempba_balancing_policy_t policy);

    /**
     * Installs the process-wide MT node manager bound to `lb`.  Wraps gempba::mt::create_node_manager.  Returns NULL
     * with last-error set if a node manager already exists.
     */
    gempba_node_manager_t gempba_mt_create_node_manager(gempba_load_balancer_t lb);

    /**
     * Creates a child node whose runnable is captured at construction time.  Wraps gempba::mt::create_explicit_node.
     *
     * `parent` must not be NULL — substitute gempba_create_dummy_node in the binding when the runnable receives a
     * NULL parent.  user_data lifetime follows gempba_release_fn.
     */
    gempba_status_t gempba_mt_create_explicit_node(gempba_load_balancer_t lb, gempba_node_t parent,
                                                   gempba_runnable_fn runnable, void* user_data,
                                                   gempba_release_fn release_user_data, /* nullable */
                                                   gempba_bytes_t args, gempba_node_t* out);

    /**
     * Creates a child node whose args are produced lazily.  Wraps gempba::mt::create_lazy_node.
     *
     * The framework calls `lazy_args` immediately before the runnable runs; if it reports `produced = 0` the node is
     * pruned and `runnable` is never invoked.  Both callbacks share the same user_data, so a single release_fn frees
     * it.
     */
    gempba_status_t gempba_mt_create_lazy_node(gempba_load_balancer_t lb, gempba_node_t parent,
                                               gempba_runnable_fn runnable, gempba_lazy_args_fn lazy_args,
                                               void* user_data, gempba_release_fn release_user_data, /* nullable */
                                               gempba_node_t* out);

    /* ─── Multiprocess engine (mp) ──────────────────────────────────────────── */

    /**
     * Installs the MP load balancer.  Wraps gempba::mp::create_load_balancer.  `worker` may be NULL when the binding
     * only intends single-process work.
     */
    gempba_load_balancer_t gempba_mp_create_load_balancer(gempba_balancing_policy_t policy,
                                                          gempba_scheduler_worker_t worker /* nullable */);

    /**
     * Installs the MP node manager.  Wraps gempba::mp::create_node_manager.  `worker` may be NULL — see
     * gempba_mp_create_load_balancer.
     */
    gempba_node_manager_t gempba_mp_create_node_manager(gempba_load_balancer_t    lb,
                                                        gempba_scheduler_worker_t worker /* nullable */);

    /**
     * Creates an MP child node.  Wraps gempba::mp::create_explicit_node with identity ser/deser — args are already
     * serialised on the binding side, the core just shuffles task_packets through.  See
     * gempba_mt_create_explicit_node for the parent / user_data / release_fn contract.
     */
    gempba_status_t gempba_mp_create_explicit_node(gempba_load_balancer_t lb, gempba_node_t parent,
                                                   gempba_runnable_fn runnable, void* user_data,
                                                   gempba_release_fn release_user_data, /* nullable */
                                                   gempba_bytes_t args, gempba_node_t* out);

    /* ─── Node manager ──────────────────────────────────────────────────────── */

    /** Wraps gempba::node_manager::set_goal.  Sets both the optimisation direction and the score storage kind in one
     * call. */
    void gempba_nm_set_goal(gempba_node_manager_t nm, gempba_goal_t goal, gempba_score_type_t score_kind);

    /** Wraps gempba::node_manager::set_balancing_policy. */
    void gempba_nm_set_balancing_policy(gempba_node_manager_t nm, gempba_balancing_policy_t policy);

    /** Wraps gempba::node_manager::set_thread_pool_size.  Resizes the underlying BS::thread_pool — call before
     * submitting work. */
    void gempba_nm_set_thread_pool_size(gempba_node_manager_t nm, uint32_t n);

    /**
     * Wraps gempba::node_manager::try_local_submit.  Sets *out_accepted to 1 iff the load balancer accepted the node
     * for local execution; 0 means it was forwarded.  The status itself reports ABI / exception failure, not the
     * accept/forward decision.
     */
    gempba_status_t gempba_nm_try_local_submit(gempba_node_manager_t nm, gempba_node_t node,
                                               gempba_bool_t* out_accepted);

    /** Wraps gempba::node_manager::forward.  Hands the node to the load balancer for forwarding without local
     * execution. */
    gempba_status_t gempba_nm_forward(gempba_node_manager_t nm, gempba_node_t node);

    /**
     * Wraps gempba::node_manager::try_update_score_and_invalidate_result.  Returns 1 if the candidate score improved
     * on the current best (and the stored result was therefore invalidated), 0 otherwise.  raw_bits/kind round through
     * gempba::score::from_raw.
     */
    gempba_bool_t gempba_nm_try_update_score(gempba_node_manager_t nm, int64_t raw_bits, gempba_score_type_t kind);

    /**
     * Wraps gempba::node_manager::try_update_result.  Atomically updates score and result iff the candidate score
     * improves on the current best.  Returns 1 on commit, 0 if the score did not improve.
     */
    gempba_bool_t gempba_nm_try_update_result(gempba_node_manager_t nm, gempba_bytes_t result, int64_t raw_bits,
                                              gempba_score_type_t kind);

    /** gempba::node_manager::get_score().to_raw() / .kind().  Pair them when the caller needs to reconstruct the score
     * type-correctly. */
    int64_t             gempba_nm_get_score_raw(gempba_node_manager_t nm);
    gempba_score_type_t gempba_nm_get_score_kind(gempba_node_manager_t nm);

    /** Wraps gempba::node_manager::set_score.  Replaces the stored score unconditionally; bindings that need
     * conditional update should call gempba_nm_try_update_score. */
    void gempba_nm_set_score(gempba_node_manager_t nm, int64_t raw_bits, gempba_score_type_t kind);

    /**
     * Wraps gempba::node_manager::get_result_bytes.  *out is {NULL, 0} if no result is set.  Caller frees *out via
     * gempba_buffer_free if non-empty.
     */
    gempba_status_t gempba_nm_get_result(gempba_node_manager_t nm, gempba_buffer_t* out);

    /** gempba::node_manager::generate_unique_id. */
    uint32_t gempba_nm_generate_unique_id(gempba_node_manager_t nm);

    /** gempba::node_manager::get_wall_time (static). */
    double gempba_nm_wall_time(void);

    /** gempba::node_manager::rank_me — non-zero only in MP mode. */
    int32_t gempba_nm_rank_me(gempba_node_manager_t nm);

    /** gempba::node_manager::get_thread_request_count — cumulative submit count. */
    int64_t gempba_nm_thread_request_count(gempba_node_manager_t nm);

    /** gempba::node_manager::get_idle_time — wall-clock seconds the underlying thread pool spent idle. */
    double gempba_nm_idle_time(gempba_node_manager_t nm);

    /** gempba::node_manager::get_balancing_policy. */
    gempba_balancing_policy_t gempba_nm_get_balancing_policy(gempba_node_manager_t nm);

    /**
     * Wraps gempba::node_manager::try_remote_submit.  Asks the scheduler to ship the node to another rank under the
     * given runnable id.  *out_accepted is 1 iff the scheduler took it; the runnable id must match a registered
     * gempba_serial_runnable on the receiving rank.
     */
    gempba_status_t gempba_nm_try_remote_submit(gempba_node_manager_t nm, gempba_node_t node, int32_t runnable_id,
                                                gempba_bool_t* out_accepted);

    /** gempba::node_manager::is_done — non-blocking pool-state check. */
    gempba_bool_t gempba_nm_is_done(gempba_node_manager_t nm);

    /** gempba::node_manager::wait — blocks until the underlying thread pool is drained.  Returns GEMPBA_ERR_RUNTIME if
     * the wait raised an exception. */
    gempba_status_t gempba_nm_wait(gempba_node_manager_t nm);

    /* ─── Serial runnable (mp) ──────────────────────────────────────────────── */

    /**
     * Registers a runnable identified by `id` for cross-rank dispatch.  Wraps either
     * gempba::mp::runnables::return_none::create<task_packet> (when returns_value == 0) or
     * return_value::create<task_packet, task_packet>.
     *
     * The C ABI uses identity ser/deser internally — args/results are raw bytes.  user_data + release_user_data follow
     * the gempba_release_fn rule: the release fires exactly once when the runnable is dropped (or immediately on
     * creation failure).
     */
    gempba_status_t gempba_serial_runnable_create(int32_t id, gempba_bool_t returns_value, gempba_runnable_fn runnable,
                                                  void* user_data, gempba_release_fn release_user_data, /* nullable */
                                                  gempba_serial_runnable_t* out);

    /** Releases the runnable wrapper and decrements the underlying shared_ptr. */
    void gempba_serial_runnable_destroy(gempba_serial_runnable_t sr);

    /**
     * Invokes the runnable against a node manager.
     *   - Void runnables return *out = {NULL, 0} immediately (async submit).
     *   - Non-void runnables block until the result is ready and populate *out.
     * Caller frees *out via gempba_buffer_free if non-empty.
     */
    gempba_status_t gempba_serial_runnable_invoke(gempba_serial_runnable_t sr, gempba_node_manager_t nm,
                                                  gempba_bytes_t args, gempba_buffer_t* out);

    /* ─── Scheduler (mp) ────────────────────────────────────────────────────── */

    /**
     * Creates the process-wide scheduler with the given topology and timeout (seconds).  Wraps
     * gempba::mp::create_scheduler.  Calling MPI must already be initialised before this; the scheduler does not own
     * the MPI lifecycle.
     */
    gempba_status_t gempba_scheduler_create(gempba_scheduler_topology_t topology, double timeout,
                                            gempba_scheduler_t* out);

    /** Tears down the scheduler.  Releases the C++ object held inside the wrapper; MPI itself is unaffected. */
    void gempba_scheduler_destroy(gempba_scheduler_t s);

    /** gempba::scheduler::barrier — collective; every rank must reach it. */
    void gempba_scheduler_barrier(gempba_scheduler_t s);

    /** gempba::scheduler::rank_me / world_size. */
    int32_t gempba_scheduler_rank_me(gempba_scheduler_t s);
    int32_t gempba_scheduler_world_size(gempba_scheduler_t s);

    /** gempba::scheduler::set_goal — sets goal + score kind for the centralised tournament.  Must be called on every
     * rank with the same arguments. */
    void gempba_scheduler_set_goal(gempba_scheduler_t s, gempba_goal_t goal, gempba_score_type_t kind);

    /** gempba::scheduler::elapsed_time — seconds since the scheduler started. */
    double gempba_scheduler_elapsed_time(gempba_scheduler_t s);

    /** gempba::scheduler::synchronize_stats — collective; gathers per-rank stats onto rank 0.  Visit them via
     * gempba_scheduler_visit_stats. */
    void gempba_scheduler_synchronize_stats(gempba_scheduler_t s);

    /** Borrowed views into the scheduler — never free.  Only one role is meaningful per rank: center on rank 0, worker
     * on every other rank. */
    gempba_scheduler_center_t gempba_scheduler_center_view(gempba_scheduler_t s);
    gempba_scheduler_worker_t gempba_scheduler_worker_view(gempba_scheduler_t s);

    /* ── Stats: callback-based iteration (no allocation, language-neutral) ──── */

    /** Discriminator for gempba_stat_value_t.v — picked to round-trip the scalar types gempba::stats::visit emits
     * without losing precision. */
    typedef enum {
        GEMPBA_STAT_INT32  = 0,
        GEMPBA_STAT_INT64  = 1,
        GEMPBA_STAT_DOUBLE = 2
    } gempba_stat_kind_t;

    /** Tagged union carrying one stat reading. */
    typedef struct {
        gempba_stat_kind_t kind;
        union {
            int32_t i32;
            int64_t i64;
            double  f64;
        } v;
    } gempba_stat_value_t;

    /**
     * Visitor invoked once per (rank, label, value) tuple.
     *   label : UTF-8, NUL-terminated; valid only for the call.
     */
    typedef void (*gempba_stat_visitor_fn)(void* user_data, int32_t rank, const char* label,
                                           const gempba_stat_value_t* value);

    /**
     * Drives the visitor over every (rank, label, value) tuple in the stats vector — wraps
     * gempba::scheduler::get_stats_vector + per-stats visit().  Call gempba_scheduler_synchronize_stats first to
     * populate non-zero ranks.  Bindings that need a flat list build it inside `visitor`.
     */
    void gempba_scheduler_visit_stats(gempba_scheduler_t s, gempba_stat_visitor_fn visitor, void* user_data);

    /* ── Scheduler center ───────────────────────────────────────────────────── */

    /** Pass-throughs to gempba::scheduler::center.  Same semantics as the scheduler-level versions — the views just
     * disambiguate role. */
    void    gempba_scheduler_center_barrier(gempba_scheduler_center_t c);
    int32_t gempba_scheduler_center_rank_me(gempba_scheduler_center_t c);
    int32_t gempba_scheduler_center_world_size(gempba_scheduler_center_t c);

    /**
     * Wraps gempba::scheduler::center::run.  Drives the centralised work-loop: sends `task` as the seed under
     * `runnable_id`, then orchestrates the tournament until every rank goes idle.  Blocks until termination.
     */
    gempba_status_t gempba_scheduler_center_run(gempba_scheduler_center_t c, gempba_bytes_t task, int32_t runnable_id);

    /** Wraps gempba::scheduler::center::get_result.  *out is the best result gathered from all ranks, or {NULL, 0} if
     * none was produced.  Caller frees *out via gempba_buffer_free if non-empty. */
    gempba_status_t gempba_scheduler_center_get_result(gempba_scheduler_center_t c, gempba_buffer_t* out);

    /**
     * Visits each result in order.  Visitor receives borrowed bytes valid only for the call — copy if retention is
     * needed.
     */
    typedef void (*gempba_result_visitor_fn)(void* user_data, size_t index, gempba_bytes_t bytes);

    /** Wraps gempba::scheduler::center::get_all_results.  Iterates results in rank order; index 0 is rank 0 (the
     * center, normally null). */
    void gempba_scheduler_center_visit_all_results(gempba_scheduler_center_t c, gempba_result_visitor_fn visitor,
                                                   void* user_data);

    /* ── Scheduler worker ───────────────────────────────────────────────────── */

    /** Pass-throughs to gempba::scheduler::worker. */
    void    gempba_scheduler_worker_barrier(gempba_scheduler_worker_t w);
    int32_t gempba_scheduler_worker_rank_me(gempba_scheduler_worker_t w);
    int32_t gempba_scheduler_worker_world_size(gempba_scheduler_worker_t w);

    /**
     * Wraps gempba::scheduler::worker::run.  Drives the worker loop on a non-center rank: registers the runnables
     * (paired ids[i] / runnables[i] for i in [0, count)), then blocks until the center notifies termination.
     */
    gempba_status_t gempba_scheduler_worker_run(gempba_scheduler_worker_t w, gempba_node_manager_t nm,
                                                const int32_t* ids, const gempba_serial_runnable_t* runnables,
                                                size_t count);

    /** ─── Telemetry (process-wide, no handle) ───────────────────────────────────
     *
     * Mirrors gempba::telemetry::{enable,disable,is_enabled}.  The flag is process-local and sticky: it lives in this
     * process only and remains in effect across mp::create_* calls until explicitly toggled.
     *
     * In multi-process (mp) mode the flag MUST be flipped symmetrically on every process — every process or no
     * process.  The telemetry install path goes through a collective IPC handshake; an asymmetric call leaves the
     * still-enabled processes blocked indefinitely.  The same symmetry requirement applies to teardown.
     *
     * Recommended pattern (mp), to suppress telemetry before any create_*:
     *     gempba_telemetry_disable();      // every process, before create_*
     *     gempba_status_t st = gempba_scheduler_create(...);
     */

    /**
     * Tear down any running telemetry hub on this process and suppress subsequent auto-installs from
     * gempba_mp_create_* until gempba_telemetry_enable is called.  Idempotent.  Safe to call before any hub has been
     * installed.
     */
    void gempba_telemetry_disable(void);

    /**
     * Lift a previous gempba_telemetry_disable so subsequent gempba_mp_create_* calls install the telemetry hub again.
     * Idempotent.  Does not itself install a hub.
     */
    void gempba_telemetry_enable(void);

    /**
     * Returns 1 while installs are allowed (default), 0 after disable and before the matching enable.  Reflects only
     * this process's flag.
     */
    gempba_bool_t gempba_telemetry_is_enabled(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

// NOLINTEND(readability-identifier-naming)

#endif /* GEMPBA_CABI_GEMPBA_H */
