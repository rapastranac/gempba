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
#include <memory>
#include <stdexcept>

#include <cabi/internals.hpp>
#include <gempba/cabi/gempba.h>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>

using gempba::cabi::detail::g_last_error;
using gempba::cabi::detail::set_last_error;

// NOLINTBEGIN(readability-identifier-naming)

/**
 * load_balancer / node_manager handles are reinterpret_cast directly from
 * their C++ counterparts; they never live inside a wrapper because lifetime
 * is owned by gempba (singletons / views).
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

#endif // !GEMPBA_MULTIPROCESSING || GEMPBA_DEV_MODE

    /** ── telemetry (process-wide flag) ─────────────────────────────────────────
     * The C++ functions are noexcept; no try/catch envelope needed. */

    void gempba_telemetry_disable(void) { gempba::telemetry::disable(); }
    void gempba_telemetry_enable(void) { gempba::telemetry::enable(); }
    gempba_bool_t gempba_telemetry_is_enabled(void) { return gempba::telemetry::is_enabled() ? 1 : 0; }

} // extern "C"

// NOLINTEND(readability-identifier-naming)
