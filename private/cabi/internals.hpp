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

#include <string>

#include <gempba/cabi/gempba.h>
#include <gempba/gempba.hpp>

/**
 * Opaque-handle definitions, at global scope so the C-linkage `typedef struct
 * gempba_xxx_s* gempba_xxx_t` declarations in gempba.h refer to the same tag.
 */
struct gempba_node_s {
    gempba::node cpp;
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

} // namespace gempba::cabi::detail

// NOLINTEND(readability-identifier-naming)

#endif // GEMPBA_CABI_INTERNALS_HPP
