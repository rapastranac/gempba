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
 * Implementation-detail helpers shared by the C ABI translator
 * (src/cabi/gempba.cpp) and its unit tests.  Not part of the public surface
 * — sits under private/ so consumers never see it.  All entities live in
 * gempba::cabi::detail so they can't collide with the C-linkage symbols
 * exposed by gempba.h.
 */

#ifndef GEMPBA_CABI_INTERNALS_HPP
#define GEMPBA_CABI_INTERNALS_HPP

// NOLINTBEGIN(readability-identifier-naming)
// C ABI surface — see src/cabi/gempba.cpp for the matching suppression.

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>

#include <gempba/cabi/gempba.h>
#include <gempba/core/scheduler.hpp>
#include <gempba/core/serial_runnable.hpp>
#include <gempba/gempba.hpp>
#include <gempba/utils/score.hpp>
#include <gempba/utils/task_packet.hpp>

/**
 * Opaque-handle definitions, at global scope so the C-linkage `typedef struct
 * gempba_xxx_s* gempba_xxx_t` declarations in gempba.h refer to the same tag.
 *
 * Both wrappers intentionally lack a custom destructor.  The release_fn for
 * user_data lives on the shared_ptr<user_data_holder> owned by the runnable's
 * std::function captures (see make_holder); destroying the wrapper drops the
 * gempba::node / serial_runnable, which drops the runnable, which decrements
 * the holder.  The release_fn fires exactly once when the LAST std::function
 * copy is gone — calling it from the wrapper destructor would double-release.
 */
struct gempba_node_s {
    gempba::node cpp;
};

struct gempba_serial_runnable_s {
    std::shared_ptr<gempba::serial_runnable> cpp;
};

struct gempba_scheduler_s {
    gempba::scheduler* cpp = nullptr;
};

namespace gempba::cabi::detail {

    /**
     * Thread-local last-error string, shared across every translation unit that
     * includes this header.  Wrapped in a function returning a reference (the
     * "Meyers' singleton" pattern, applied to thread_local) so the underlying
     * storage has exactly one definition regardless of how many TUs include
     * this header — `inline thread_local` at namespace scope trips mingw-gcc's
     * per-TU TLS-init-function emission and links double.
     */
    inline std::string& g_last_error() {
        thread_local std::string s;
        return s;
    }

    inline void set_last_error(const char* msg) { g_last_error() = msg ? msg : ""; }

    /**
     * Holder so a callback's user_data + release_fn live for exactly one
     * release across all std::function copies of the wrapped runnable.  The
     * shared_ptr's custom deleter fires release_fn when the last copy is
     * dropped.
     */
    struct user_data_holder {
        void* data = nullptr;
        gempba_release_fn release = nullptr;
    };

    using holder_ptr = std::shared_ptr<user_data_holder>;

    inline holder_ptr make_holder(void* data, gempba_release_fn release) {
        return holder_ptr(new user_data_holder{data, release}, [](user_data_holder* h) {
            if (h->release && h->data)
                h->release(h->data);
            delete h;
        });
    }

    /**
     * Build a holder that consumes user_data even if its own allocation fails.
     * Guarantees that every fallible factory which receives user_data has a
     * single, predictable place where release_fn fires — regardless of which
     * allocation along the path threw.  Bindings can rely on the rule
     * "user_data is consumed once create_* returns, success or failure".
     */
    inline holder_ptr safe_make_holder(void* data, gempba_release_fn release) {
        try {
            return make_holder(data, release);
        } catch (...) {
            if (release && data)
                release(data);
            throw;
        }
    }

    inline gempba::task_packet packet_from_borrowed(gempba_bytes_t bytes) {
        if (bytes.len == 0 || bytes.data == nullptr)
            return gempba::task_packet::EMPTY;
        gempba::task_packet pkt(bytes.len);
        std::memcpy(pkt.data(), bytes.data, bytes.len);
        return pkt;
    }

    inline gempba::task_packet packet_from_owned(gempba_buffer_t* buf) {
        if (buf == nullptr || buf->data == nullptr || buf->len == 0) {
            if (buf)
                gempba_buffer_free(buf);
            return gempba::task_packet::EMPTY;
        }
        gempba::task_packet pkt(buf->len);
        std::memcpy(pkt.data(), buf->data, buf->len);
        gempba_buffer_free(buf);
        return pkt;
    }

    inline gempba_buffer_t buffer_from_packet(const gempba::task_packet& pkt) {
        if (pkt.empty())
            return gempba_buffer_t{nullptr, 0};
        gempba_buffer_t out = gempba_buffer_alloc(pkt.size());
        if (out.data == nullptr) {
            set_last_error("gempba_buffer_alloc returned NULL");
            return out;
        }
        std::memcpy(out.data, pkt.data(), pkt.size());
        return out;
    }

    inline std::function<gempba::task_packet(std::thread::id, gempba::task_packet, gempba::node)> make_cpp_runnable(const holder_ptr& holder, gempba_runnable_fn fn,
                                                                                                                    gempba_node_t self_handle) {
        return [holder, fn, self_handle](std::thread::id tid, gempba::task_packet args, const gempba::node& parent_or_null) -> gempba::task_packet {
            const std::uint64_t hashed_tid = std::hash<std::thread::id>{}(tid);

            gempba_bytes_t c_args{args.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(args.data()), args.size()};
            gempba_node_t c_parent = (parent_or_null == nullptr) ? nullptr : self_handle;
            gempba_buffer_t c_out{nullptr, 0};

            const gempba_status_t st = fn(holder->data, hashed_tid, c_args, c_parent, &c_out);
            if (st != GEMPBA_OK) {
                if (c_out.data)
                    gempba_buffer_free(&c_out);
                throw std::runtime_error("gempba cabi: user runnable returned status " + std::to_string(static_cast<int>(st)));
            }
            return packet_from_owned(&c_out);
        };
    }

    inline std::function<std::optional<std::tuple<gempba::task_packet>>()> make_cpp_lazy_args(const holder_ptr& holder, gempba_lazy_args_fn fn) {
        return [holder, fn]() -> std::optional<std::tuple<gempba::task_packet>> {
            gempba_bool_t produced = 0;
            gempba_buffer_t c_out{nullptr, 0};
            const gempba_status_t st = fn(holder->data, &produced, &c_out);
            if (st != GEMPBA_OK || produced == 0) {
                if (c_out.data)
                    gempba_buffer_free(&c_out);
                return std::nullopt;
            }
            return std::make_optional(std::make_tuple(packet_from_owned(&c_out)));
        };
    }

    /**
     * Identity ser / deser for mp::create_explicit_node and serial_runnables.
     * args / results are already serialised on the binding side, so the core
     * just shuffles task_packets through.
     */
    inline std::function<gempba::task_packet(gempba::task_packet)> identity_ser() {
        return [](gempba::task_packet pkt) { return pkt; };
    }

    inline std::function<std::tuple<gempba::task_packet>(gempba::task_packet)> identity_deser_value() {
        return [](const gempba::task_packet& pkt) { return std::make_tuple(pkt); };
    }

    inline std::function<std::tuple<gempba::task_packet>(const gempba::task_packet&&)> identity_deser_rvalue() {
        // Signature receives `const T&&` (set by the gempba::serial_runnable
        // interface), so std::move is a no-op — drop it and copy explicitly.
        return [](const gempba::task_packet&& pkt) { return std::make_tuple(pkt); };
    }

    inline gempba::score score_from_raw(int64_t raw_bits, gempba_score_type_t kind) { return gempba::score::from_raw(static_cast<gempba::score_type>(kind), raw_bits); }

} // namespace gempba::cabi::detail

// NOLINTEND(readability-identifier-naming)

#endif // GEMPBA_CABI_INTERNALS_HPP
