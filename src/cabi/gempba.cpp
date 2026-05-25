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

#include <cstdlib>
#include <map>
#include <memory>
#include <stdexcept>

#include <cabi/internals.hpp>
#include <gempba/cabi/gempba.h>
#include <gempba/detail/runnables/serial_runnable_non_void.hpp>
#include <gempba/detail/runnables/serial_runnable_void.hpp>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>

using gempba::cabi::detail::buffer_from_packet;
using gempba::cabi::detail::g_last_error;
using gempba::cabi::detail::holder_ptr;
using gempba::cabi::detail::identity_deser_rvalue;
using gempba::cabi::detail::identity_deser_value;
using gempba::cabi::detail::identity_ser;
using gempba::cabi::detail::make_cpp_lazy_args;
using gempba::cabi::detail::make_cpp_runnable;
using gempba::cabi::detail::packet_from_borrowed;
using gempba::cabi::detail::packet_from_owned;
using gempba::cabi::detail::safe_make_holder;
using gempba::cabi::detail::score_from_raw;
using gempba::cabi::detail::set_last_error;

// NOLINTBEGIN(readability-identifier-naming)

/**
 * load_balancer / node_manager / scheduler_center / scheduler_worker handles
 * are reinterpret_cast directly from their C++ counterparts; they never live
 * inside a wrapper because lifetime is owned by gempba (singletons / views).
 */

/* ─── Macro: try / catch envelope for fallible entry points ─────────────── */

#define GEMPBA_TRY                                                                                                                                                                           \
    try {                                                                                                                                                                                    \
        gempba_clear_last_error();

#define GEMPBA_CATCH_RETURN_STATUS                                                                                                                                                           \
    }                                                                                                                                                                                        \
    catch (const std::bad_alloc&) {                                                                                                                                                          \
        set_last_error("std::bad_alloc");                                                                                                                                                    \
        return GEMPBA_ERR_OUT_OF_MEMORY;                                                                                                                                                     \
    }                                                                                                                                                                                        \
    catch (const std::invalid_argument& e) {                                                                                                                                                 \
        set_last_error(e.what());                                                                                                                                                            \
        return GEMPBA_ERR_INVALID_ARG;                                                                                                                                                       \
    }                                                                                                                                                                                        \
    catch (const std::exception& e) {                                                                                                                                                        \
        set_last_error(e.what());                                                                                                                                                            \
        return GEMPBA_ERR_RUNTIME;                                                                                                                                                           \
    }                                                                                                                                                                                        \
    catch (...) {                                                                                                                                                                            \
        set_last_error("unknown C++ exception");                                                                                                                                             \
        return GEMPBA_ERR_UNKNOWN;                                                                                                                                                           \
    }

/** For void entry points, swallow the exception into the thread-local error. */
#define GEMPBA_CATCH_VOID                                                                                                                                                                    \
    }                                                                                                                                                                                        \
    catch (const std::exception& e) {                                                                                                                                                        \
        set_last_error(e.what());                                                                                                                                                            \
    }                                                                                                                                                                                        \
    catch (...) {                                                                                                                                                                            \
        set_last_error("unknown C++ exception");                                                                                                                                             \
    }

/** For factory functions that return a handle directly (not a status):
 * on exception, set last_error and return nullptr.  Bindings check the
 * return for nullptr and call gempba_last_error_message() on failure. */
#define GEMPBA_CATCH_RETURN_NULL                                                                                                                                                             \
    }                                                                                                                                                                                        \
    catch (const std::exception& e) {                                                                                                                                                        \
        set_last_error(e.what());                                                                                                                                                            \
        return nullptr;                                                                                                                                                                      \
    }                                                                                                                                                                                        \
    catch (...) {                                                                                                                                                                            \
        set_last_error("unknown C++ exception");                                                                                                                                             \
        return nullptr;                                                                                                                                                                      \
    }

/* ─── extern "C" surface ────────────────────────────────────────────────── */

extern "C" {

    /* ── Status & error ─────────────────────────────────────────────────────── */

    const char* gempba_last_error_message(void) { return g_last_error().empty() ? nullptr : g_last_error().c_str(); }

    void gempba_clear_last_error(void) { g_last_error().clear(); }

    /* ── Buffers ────────────────────────────────────────────────────────────── */

    gempba_buffer_t gempba_buffer_alloc(size_t len) {
        if (len == 0)
            return gempba_buffer_t{nullptr, 0};
        auto* p = static_cast<std::uint8_t*>(std::malloc(len));
        if (!p)
            return gempba_buffer_t{nullptr, 0};
        return gempba_buffer_t{p, len};
    }

    void gempba_buffer_free(gempba_buffer_t* buf) {
        if (!buf)
            return;
        if (buf->data)
            std::free(buf->data);
        buf->data = nullptr;
        buf->len = 0;
    }

    /* ── Globals ────────────────────────────────────────────────────────────── */

    gempba_load_balancer_t gempba_get_load_balancer(void) {
        /** Returns nullptr when not yet created — safe, no exception path. */
        return reinterpret_cast<gempba_load_balancer_t>(gempba::get_load_balancer());
    }

    gempba_node_manager_t gempba_get_node_manager(void) {
        GEMPBA_TRY
            return reinterpret_cast<gempba_node_manager_t>(&gempba::get_node_manager());
        GEMPBA_CATCH_RETURN_NULL
    }

    int32_t gempba_shutdown(void) {
        try {
            return static_cast<int32_t>(gempba::shutdown());
        } catch (const std::exception& e) {
            set_last_error(e.what());
            return -1;
        } catch (...) {
            set_last_error("unknown C++ exception");
            return -1;
        }
    }

    /* ── Node creation (engine-agnostic) ───────────────────────────────────── */

    gempba_status_t gempba_create_dummy_node(gempba_load_balancer_t lb, gempba_node_t* out) {
        GEMPBA_TRY
            if (out == nullptr) {
                set_last_error("out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            auto wrapper = std::make_unique<gempba_node_s>();
            wrapper->cpp = gempba::create_dummy_node(*lb_cpp);
            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_status_t gempba_create_seed_node(gempba_load_balancer_t lb, gempba_runnable_fn runnable, void* user_data, gempba_release_fn release_user_data, gempba_bytes_t args,
                                            gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr) {
                set_last_error("out or runnable is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            gempba::task_packet args_pkt = packet_from_borrowed(args);

            wrapper->cpp = gempba::create_seed_node<gempba::task_packet>(*lb_cpp, std::move(cpp_fn), std::make_tuple(std::move(args_pkt)));

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    void gempba_node_destroy(gempba_node_t node) { delete node; }

    /* ── multithreading ─────────────────────────────────────────────────────── */

#if !GEMPBA_MULTIPROCESSING || GEMPBA_DEV_MODE

    gempba_load_balancer_t gempba_mt_create_load_balancer(gempba_balancing_policy_t policy) {
        GEMPBA_TRY
            auto* lb = gempba::multithreading::create_load_balancer(static_cast<gempba::balancing_policy>(policy));
            return reinterpret_cast<gempba_load_balancer_t>(lb);
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_node_manager_t gempba_mt_create_node_manager(gempba_load_balancer_t lb) {
        GEMPBA_TRY
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            return reinterpret_cast<gempba_node_manager_t>(&gempba::multithreading::create_node_manager(lb_cpp));
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_status_t gempba_mt_create_explicit_node(gempba_load_balancer_t lb, gempba_node_t parent, gempba_runnable_fn runnable, void* user_data, gempba_release_fn release_user_data,
                                                   gempba_bytes_t args, gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr || parent == nullptr) {
                set_last_error("out, runnable, or parent is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            gempba::node parent_local = parent->cpp;

            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            gempba::task_packet args_pkt = packet_from_borrowed(args);

            wrapper->cpp =
                    gempba::multithreading::create_explicit_node<gempba::task_packet, gempba::task_packet>(*lb_cpp, parent_local, std::move(cpp_fn), std::make_tuple(std::move(args_pkt)));

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_status_t gempba_mt_create_lazy_node(gempba_load_balancer_t lb, gempba_node_t parent, gempba_runnable_fn runnable, gempba_lazy_args_fn lazy_args, void* user_data,
                                               gempba_release_fn release_user_data, gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr || lazy_args == nullptr || parent == nullptr) {
                set_last_error("out, runnable, lazy_args, or parent is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            gempba::node parent_local = parent->cpp;

            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            auto cpp_lazy = make_cpp_lazy_args(holder, lazy_args);

            wrapper->cpp = gempba::multithreading::create_lazy_node<gempba::task_packet, gempba::task_packet>(*lb_cpp, parent_local, std::move(cpp_fn), std::move(cpp_lazy));

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

#else

    gempba_load_balancer_t gempba_mt_create_load_balancer(gempba_balancing_policy_t policy) {
        GEMPBA_TRY
            auto* lb = gempba::multiprocessing::create_load_balancer(static_cast<gempba::balancing_policy>(policy), nullptr);
            return reinterpret_cast<gempba_load_balancer_t>(lb);
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_node_manager_t gempba_mt_create_node_manager(gempba_load_balancer_t lb) {
        GEMPBA_TRY
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            return reinterpret_cast<gempba_node_manager_t>(&gempba::multiprocessing::create_node_manager(lb_cpp, nullptr));
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_status_t gempba_mt_create_explicit_node(gempba_load_balancer_t lb, gempba_node_t parent, gempba_runnable_fn runnable, void* user_data, gempba_release_fn release_user_data,
                                                   gempba_bytes_t args, gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr || parent == nullptr) {
                set_last_error("out, runnable, or parent is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            gempba::node parent_local = parent->cpp;

            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            gempba::task_packet args_pkt = packet_from_borrowed(args);

            wrapper->cpp = gempba::multiprocessing::create_explicit_node<gempba::task_packet, gempba::task_packet>(
                    *lb_cpp, parent_local, std::move(cpp_fn), std::make_tuple(std::move(args_pkt)), identity_ser(), identity_deser_value());

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_status_t gempba_mt_create_lazy_node(gempba_load_balancer_t lb, gempba_node_t parent, gempba_runnable_fn runnable, gempba_lazy_args_fn lazy_args, void* user_data,
                                               gempba_release_fn release_user_data, gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr || lazy_args == nullptr || parent == nullptr) {
                set_last_error("out, runnable, lazy_args, or parent is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            gempba::node parent_local = parent->cpp;

            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            auto cpp_lazy = make_cpp_lazy_args(holder, lazy_args);

            wrapper->cpp = gempba::multiprocessing::create_lazy_node<gempba::task_packet, gempba::task_packet>(*lb_cpp, parent_local, std::move(cpp_fn), std::move(cpp_lazy), identity_ser(),
                                                                                                               identity_deser_value());

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

#endif // !GEMPBA_MULTIPROCESSING || GEMPBA_DEV_MODE

#if GEMPBA_MULTIPROCESSING

    /* ── multiprocessing ────────────────────────────────────────────────────── */

    gempba_load_balancer_t gempba_mp_create_load_balancer(gempba_balancing_policy_t policy, gempba_scheduler_worker_t worker) {
        GEMPBA_TRY
            auto* w = reinterpret_cast<gempba::scheduler::worker*>(worker);
            auto* lb = gempba::multiprocessing::create_load_balancer(static_cast<gempba::balancing_policy>(policy), w);
            return reinterpret_cast<gempba_load_balancer_t>(lb);
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_node_manager_t gempba_mp_create_node_manager(gempba_load_balancer_t lb, gempba_scheduler_worker_t worker) {
        GEMPBA_TRY
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            auto* w = reinterpret_cast<gempba::scheduler::worker*>(worker);
            return reinterpret_cast<gempba_node_manager_t>(&gempba::multiprocessing::create_node_manager(lb_cpp, w));
        GEMPBA_CATCH_RETURN_NULL
    }

    gempba_status_t gempba_mp_create_explicit_node(gempba_load_balancer_t lb, gempba_node_t parent, gempba_runnable_fn runnable, void* user_data, gempba_release_fn release_user_data,
                                                   gempba_bytes_t args, gempba_node_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr || parent == nullptr) {
                set_last_error("out, runnable, or parent is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* lb_cpp = reinterpret_cast<gempba::load_balancer*>(lb);
            gempba::node parent_local = parent->cpp;

            auto wrapper = std::make_unique<gempba_node_s>();
            gempba_node_t self = wrapper.get();

            auto cpp_fn = make_cpp_runnable(holder, runnable, self);
            gempba::task_packet args_pkt = packet_from_borrowed(args);

            wrapper->cpp = gempba::multiprocessing::create_explicit_node<gempba::task_packet, gempba::task_packet>(
                    *lb_cpp, parent_local, std::move(cpp_fn), std::make_tuple(std::move(args_pkt)), identity_ser(), identity_deser_value());

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

#endif // GEMPBA_MULTIPROCESSING

    /* ── node_manager ──────────────────────────────────────────────────────── */

    void gempba_nm_set_goal(gempba_node_manager_t nm, gempba_goal_t goal, gempba_score_type_t kind) {
        GEMPBA_TRY
            reinterpret_cast<gempba::node_manager*>(nm)->set_goal(static_cast<gempba::goal>(goal), static_cast<gempba::score_type>(kind));
        GEMPBA_CATCH_VOID
    }

    void gempba_nm_set_balancing_policy(gempba_node_manager_t nm, gempba_balancing_policy_t policy) {
        GEMPBA_TRY
            reinterpret_cast<gempba::node_manager*>(nm)->set_balancing_policy(static_cast<gempba::balancing_policy>(policy));
        GEMPBA_CATCH_VOID
    }

    void gempba_nm_set_thread_pool_size(gempba_node_manager_t nm, uint32_t n) {
        GEMPBA_TRY
            reinterpret_cast<gempba::node_manager*>(nm)->set_thread_pool_size(static_cast<unsigned int>(n));
        GEMPBA_CATCH_VOID
    }

    gempba_status_t gempba_nm_try_local_submit(gempba_node_manager_t nm, gempba_node_t node, gempba_bool_t* out_accepted) {
        GEMPBA_TRY
            if (node == nullptr || out_accepted == nullptr) {
                set_last_error("node or out_accepted is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            gempba::node node_local = node->cpp;
            bool ok = reinterpret_cast<gempba::node_manager*>(nm)->try_local_submit(node_local);
            *out_accepted = ok ? 1 : 0;
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_status_t gempba_nm_forward(gempba_node_manager_t nm, gempba_node_t node) {
        GEMPBA_TRY
            if (node == nullptr) {
                set_last_error("node is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            gempba::node node_local = node->cpp;
            reinterpret_cast<gempba::node_manager*>(nm)->forward(node_local);
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_bool_t gempba_nm_try_update_score(gempba_node_manager_t nm, int64_t raw_bits, gempba_score_type_t kind) {
        try {
            const auto ok = reinterpret_cast<gempba::node_manager*>(nm)->try_update_score_and_invalidate_result(score_from_raw(raw_bits, kind));
            return ok ? 1 : 0;
        } catch (const std::exception& e) {
            set_last_error(e.what());
            return 0;
        } catch (...) {
            set_last_error("unknown C++ exception");
            return 0;
        }
    }

    gempba_bool_t gempba_nm_try_update_result(gempba_node_manager_t nm, gempba_bytes_t result, int64_t raw_bits, gempba_score_type_t kind) {
        try {
            auto pkt = packet_from_borrowed(result);
            std::function<gempba::task_packet(gempba::task_packet&)> identity = [](gempba::task_packet& p) { return p; };
            const auto ok = reinterpret_cast<gempba::node_manager*>(nm)->try_update_result(pkt, score_from_raw(raw_bits, kind), identity);
            return ok ? 1 : 0;
        } catch (const std::exception& e) {
            set_last_error(e.what());
            return 0;
        } catch (...) {
            set_last_error("unknown C++ exception");
            return 0;
        }
    }

    int64_t gempba_nm_get_score_raw(gempba_node_manager_t nm) { return static_cast<int64_t>(reinterpret_cast<gempba::node_manager*>(nm)->get_score().to_raw()); }

    gempba_score_type_t gempba_nm_get_score_kind(gempba_node_manager_t nm) { return static_cast<gempba_score_type_t>(reinterpret_cast<gempba::node_manager*>(nm)->get_score().kind()); }

    void gempba_nm_set_score(gempba_node_manager_t nm, int64_t raw_bits, gempba_score_type_t kind) {
        GEMPBA_TRY
            reinterpret_cast<gempba::node_manager*>(nm)->set_score(score_from_raw(raw_bits, kind));
        GEMPBA_CATCH_VOID
    }

    gempba_status_t gempba_nm_get_result(gempba_node_manager_t nm, gempba_buffer_t* out) {
        GEMPBA_TRY
            if (out == nullptr) {
                set_last_error("out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto opt = reinterpret_cast<gempba::node_manager*>(nm)->get_result_bytes();
            if (!opt.has_value()) {
                *out = gempba_buffer_t{nullptr, 0};
                return GEMPBA_OK;
            }
            *out = buffer_from_packet(opt->get_task_packet());
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    uint32_t gempba_nm_generate_unique_id(gempba_node_manager_t nm) { return static_cast<uint32_t>(reinterpret_cast<gempba::node_manager*>(nm)->generate_unique_id()); }

    double gempba_nm_wall_time(void) { return static_cast<double>(gempba::node_manager::get_wall_time()); }

    int32_t gempba_nm_rank_me(gempba_node_manager_t nm) { return static_cast<int32_t>(reinterpret_cast<gempba::node_manager*>(nm)->rank_me()); }

    int64_t gempba_nm_thread_request_count(gempba_node_manager_t nm) { return static_cast<int64_t>(reinterpret_cast<gempba::node_manager*>(nm)->get_thread_request_count()); }

    double gempba_nm_idle_time(gempba_node_manager_t nm) { return static_cast<double>(reinterpret_cast<gempba::node_manager*>(nm)->get_idle_time()); }

    gempba_balancing_policy_t gempba_nm_get_balancing_policy(gempba_node_manager_t nm) {
        return static_cast<gempba_balancing_policy_t>(reinterpret_cast<gempba::node_manager*>(nm)->get_balancing_policy());
    }

    gempba_status_t gempba_nm_try_remote_submit(gempba_node_manager_t nm, gempba_node_t node, int32_t runnable_id, gempba_bool_t* out_accepted) {
        GEMPBA_TRY
            if (node == nullptr || out_accepted == nullptr) {
                set_last_error("node or out_accepted is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            gempba::node node_local = node->cpp;
            bool ok = reinterpret_cast<gempba::node_manager*>(nm)->try_remote_submit(node_local, static_cast<int>(runnable_id));
            *out_accepted = ok ? 1 : 0;
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_bool_t gempba_nm_is_done(gempba_node_manager_t nm) { return reinterpret_cast<gempba::node_manager*>(nm)->is_done() ? 1 : 0; }

    gempba_status_t gempba_nm_wait(gempba_node_manager_t nm) {
        GEMPBA_TRY
            reinterpret_cast<gempba::node_manager*>(nm)->wait();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

#if GEMPBA_MULTIPROCESSING

    /* ── serial_runnable (mp) ──────────────────────────────────────────────── */

    gempba_status_t gempba_serial_runnable_create(int32_t id, gempba_bool_t returns_value, gempba_runnable_fn runnable, void* user_data, gempba_release_fn release_user_data,
                                                  gempba_serial_runnable_t* out) {
        GEMPBA_TRY
            auto holder = safe_make_holder(user_data, release_user_data);
            if (out == nullptr || runnable == nullptr) {
                set_last_error("out or runnable is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto wrapper = std::make_unique<gempba_serial_runnable_s>();

            if (returns_value == 0) {
                std::function<void(std::thread::id, gempba::task_packet, gempba::node)> fn = [holder, runnable](std::thread::id tid, gempba::task_packet args, const gempba::node&) {
                    const std::uint64_t hashed = std::hash<std::thread::id>{}(tid);
                    gempba_bytes_t c_args{args.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(args.data()), args.size()};
                    gempba_buffer_t c_out{nullptr, 0};
                    runnable(holder->data, hashed, c_args, /*parent*/ nullptr, &c_out);
                    if (c_out.data)
                        gempba_buffer_free(&c_out);
                };
                wrapper->cpp = gempba::multiprocessing::runnables::return_none::create<gempba::task_packet>(static_cast<int>(id), std::move(fn), identity_deser_rvalue());
            } else {
                std::function<gempba::task_packet(std::thread::id, gempba::task_packet, gempba::node)> fn = [holder, runnable](std::thread::id tid, gempba::task_packet args,
                                                                                                                               const gempba::node&) -> gempba::task_packet {
                    const std::uint64_t hashed = std::hash<std::thread::id>{}(tid);
                    gempba_bytes_t c_args{args.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(args.data()), args.size()};
                    gempba_buffer_t c_out{nullptr, 0};
                    const gempba_status_t st = runnable(holder->data, hashed, c_args, /*parent*/ nullptr, &c_out);
                    if (st != GEMPBA_OK) {
                        if (c_out.data)
                            gempba_buffer_free(&c_out);
                        throw std::runtime_error("gempba cabi: serial runnable returned status " + std::to_string(static_cast<int>(st)));
                    }
                    return packet_from_owned(&c_out);
                };
                wrapper->cpp = gempba::multiprocessing::runnables::return_value::create<gempba::task_packet, gempba::task_packet>(static_cast<int>(id), std::move(fn),
                                                                                                                                  identity_deser_rvalue(), identity_ser());
            }

            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    void gempba_serial_runnable_destroy(gempba_serial_runnable_t sr) { delete sr; }

    gempba_status_t gempba_serial_runnable_invoke(gempba_serial_runnable_t sr, gempba_node_manager_t nm, gempba_bytes_t args, gempba_buffer_t* out) {
        GEMPBA_TRY
            if (sr == nullptr || nm == nullptr || out == nullptr) {
                set_last_error("sr, nm, or out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* nm_cpp = reinterpret_cast<gempba::node_manager*>(nm);
            gempba::task_packet args_pkt = packet_from_borrowed(args);
            auto result_opt = (*sr->cpp)(*nm_cpp, args_pkt);
            if (!result_opt.has_value()) {
                *out = gempba_buffer_t{nullptr, 0};
                return GEMPBA_OK;
            }
            *out = buffer_from_packet(result_opt->get()); // blocks
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    /* ── scheduler ─────────────────────────────────────────────────────────── */

    gempba_status_t gempba_scheduler_create(gempba_scheduler_topology_t topology, double timeout, gempba_scheduler_t* out) {
        GEMPBA_TRY
            if (out == nullptr) {
                set_last_error("out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* sched = gempba::multiprocessing::create_scheduler(static_cast<gempba::multiprocessing::scheduler_topology>(topology), timeout);
            auto wrapper = std::make_unique<gempba_scheduler_s>();
            wrapper->cpp = sched;
            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    void gempba_scheduler_destroy(gempba_scheduler_t s) {
        if (!s)
            return;
        delete s->cpp;
        delete s;
    }

    void gempba_scheduler_barrier(gempba_scheduler_t s) {
        GEMPBA_TRY
            s->cpp->barrier();
        GEMPBA_CATCH_VOID
    }

    int32_t gempba_scheduler_rank_me(gempba_scheduler_t s) { return static_cast<int32_t>(s->cpp->rank_me()); }
    int32_t gempba_scheduler_world_size(gempba_scheduler_t s) { return static_cast<int32_t>(s->cpp->world_size()); }

    void gempba_scheduler_set_goal(gempba_scheduler_t s, gempba_goal_t goal, gempba_score_type_t kind) {
        GEMPBA_TRY
            s->cpp->set_goal(static_cast<gempba::goal>(goal), static_cast<gempba::score_type>(kind));
        GEMPBA_CATCH_VOID
    }

    double gempba_scheduler_elapsed_time(gempba_scheduler_t s) { return static_cast<double>(s->cpp->elapsed_time()); }
    void gempba_scheduler_synchronize_stats(gempba_scheduler_t s) {
        GEMPBA_TRY
            s->cpp->synchronize_stats();
        GEMPBA_CATCH_VOID
    }

    gempba_scheduler_center_t gempba_scheduler_center_view(gempba_scheduler_t s) { return reinterpret_cast<gempba_scheduler_center_t>(&s->cpp->center_view()); }
    gempba_scheduler_worker_t gempba_scheduler_worker_view(gempba_scheduler_t s) { return reinterpret_cast<gempba_scheduler_worker_t>(&s->cpp->worker_view()); }

    void gempba_scheduler_visit_stats(gempba_scheduler_t s, gempba_stat_visitor_fn visitor, void* user_data) {
        if (!s || !visitor)
            return;
        GEMPBA_TRY
            auto stats_vec = s->cpp->get_stats_vector();
            const auto world_size = static_cast<int32_t>(stats_vec.size());
            for (int32_t r = 0; r < world_size; ++r) {
                stats_vec[r]->visit([&](const std::string& label, std::any&& val) {
                    gempba_stat_value_t v{};
                    if (val.type() == typeid(double)) {
                        v.kind = GEMPBA_STAT_DOUBLE;
                        v.v.f64 = std::any_cast<double>(val);
                    } else if (val.type() == typeid(float)) {
                        v.kind = GEMPBA_STAT_DOUBLE;
                        v.v.f64 = static_cast<double>(std::any_cast<float>(val));
                    } else if (val.type() == typeid(int)) {
                        v.kind = GEMPBA_STAT_INT32;
                        v.v.i32 = std::any_cast<int>(val);
                    } else if (val.type() == typeid(std::size_t)) {
                        v.kind = GEMPBA_STAT_INT64;
                        v.v.i64 = static_cast<int64_t>(std::any_cast<std::size_t>(val));
                    } else if (val.type() == typeid(long)) {
                        v.kind = GEMPBA_STAT_INT64;
                        v.v.i64 = static_cast<int64_t>(std::any_cast<long>(val));
                    } else if (val.type() == typeid(bool)) {
                        v.kind = GEMPBA_STAT_INT32;
                        v.v.i32 = std::any_cast<bool>(val) ? 1 : 0;
                    } else {
                        return;
                    }
                    visitor(user_data, r, label.c_str(), &v);
                });
            }
        GEMPBA_CATCH_VOID
    }

    /* ── scheduler::center ─────────────────────────────────────────────────── */

    void gempba_scheduler_center_barrier(gempba_scheduler_center_t c) {
        GEMPBA_TRY
            reinterpret_cast<gempba::scheduler::center*>(c)->barrier();
        GEMPBA_CATCH_VOID
    }
    int32_t gempba_scheduler_center_rank_me(gempba_scheduler_center_t c) { return static_cast<int32_t>(reinterpret_cast<gempba::scheduler::center*>(c)->rank_me()); }
    int32_t gempba_scheduler_center_world_size(gempba_scheduler_center_t c) { return static_cast<int32_t>(reinterpret_cast<gempba::scheduler::center*>(c)->world_size()); }

    gempba_status_t gempba_scheduler_center_run(gempba_scheduler_center_t c, gempba_bytes_t task, int32_t runnable_id) {
        GEMPBA_TRY
            gempba::task_packet pkt = packet_from_borrowed(task);
            reinterpret_cast<gempba::scheduler::center*>(c)->run(std::move(pkt), static_cast<int>(runnable_id));
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    gempba_status_t gempba_scheduler_center_get_result(gempba_scheduler_center_t c, gempba_buffer_t* out) {
        GEMPBA_TRY
            if (out == nullptr) {
                set_last_error("out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            gempba::task_packet pkt = reinterpret_cast<gempba::scheduler::center*>(c)->get_result();
            *out = buffer_from_packet(pkt);
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

    void gempba_scheduler_center_visit_all_results(gempba_scheduler_center_t c, gempba_result_visitor_fn visitor, void* user_data) {
        if (!c || !visitor)
            return;
        GEMPBA_TRY
            auto results = reinterpret_cast<gempba::scheduler::center*>(c)->get_all_results();
            for (size_t i = 0; i < results.size(); ++i) {
                const gempba::task_packet& pkt = results[i].get_task_packet();
                gempba_bytes_t bytes{pkt.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(pkt.data()), pkt.size()};
                visitor(user_data, i, bytes);
            }
        GEMPBA_CATCH_VOID
    }

    /* ── scheduler::worker ─────────────────────────────────────────────────── */

    void gempba_scheduler_worker_barrier(gempba_scheduler_worker_t w) {
        GEMPBA_TRY
            reinterpret_cast<gempba::scheduler::worker*>(w)->barrier();
        GEMPBA_CATCH_VOID
    }
    int32_t gempba_scheduler_worker_rank_me(gempba_scheduler_worker_t w) { return static_cast<int32_t>(reinterpret_cast<gempba::scheduler::worker*>(w)->rank_me()); }
    int32_t gempba_scheduler_worker_world_size(gempba_scheduler_worker_t w) { return static_cast<int32_t>(reinterpret_cast<gempba::scheduler::worker*>(w)->world_size()); }

    gempba_status_t gempba_scheduler_worker_run(gempba_scheduler_worker_t w, gempba_node_manager_t nm, const int32_t* ids, const gempba_serial_runnable_t* runnables, size_t count) {
        GEMPBA_TRY
            if (w == nullptr || nm == nullptr || ids == nullptr || runnables == nullptr) {
                set_last_error("w, nm, ids, or runnables is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            std::map<int, std::shared_ptr<gempba::serial_runnable>> map;
            for (size_t i = 0; i < count; ++i) {
                map[static_cast<int>(ids[i])] = runnables[i]->cpp;
            }
            reinterpret_cast<gempba::scheduler::worker*>(w)->run(*reinterpret_cast<gempba::node_manager*>(nm), std::move(map));
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

#endif // GEMPBA_MULTIPROCESSING

    /** ── telemetry (process-wide flag) ─────────────────────────────────────────
     * The C++ functions are noexcept; no try/catch envelope needed. */

    void gempba_telemetry_disable(void) { gempba::telemetry::disable(); }
    void gempba_telemetry_enable(void) { gempba::telemetry::enable(); }
    gempba_bool_t gempba_telemetry_is_enabled(void) { return gempba::telemetry::is_enabled() ? 1 : 0; }

} // extern "C"

// NOLINTEND(readability-identifier-naming)
