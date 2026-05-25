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

#include <cabi/internals.hpp>
#include <gempba/cabi/gempba.h>
#include <gempba/gempba.hpp>
#include <gempba/telemetry/telemetry_hub.hpp>

using gempba::cabi::detail::g_last_error;

// NOLINTBEGIN(readability-identifier-naming)

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

    /** ── telemetry (process-wide flag) ─────────────────────────────────────────
     * The C++ functions are noexcept; no try/catch envelope needed. */

    void gempba_telemetry_disable(void) { gempba::telemetry::disable(); }
    void gempba_telemetry_enable(void) { gempba::telemetry::enable(); }
    gempba_bool_t gempba_telemetry_is_enabled(void) { return gempba::telemetry::is_enabled() ? 1 : 0; }

} // extern "C"

// NOLINTEND(readability-identifier-naming)
