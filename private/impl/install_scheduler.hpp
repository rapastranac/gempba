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

#ifndef GEMPBA_INSTALL_SCHEDULER_HPP
#define GEMPBA_INSTALL_SCHEDULER_HPP

#include <functional>
#include <gempba/core/scheduler.hpp>
#include <memory>

namespace gempba::telemetry {
    class telemetry_transport;
}

namespace gempba::detail {

    /** @brief Makes the transport telemetry uses to reach the scheduler's other ranks. */
    using telemetry_transport_factory = std::function<std::unique_ptr<telemetry::telemetry_transport>()>;

    /**
     * @brief Keep @p p_scheduler as this process's scheduler and install the telemetry hub, which reaches the other ranks
     * through a transport from @p p_make_transport. Every rank calls it.
     *
     * @throws std::runtime_error when a scheduler already exists.
     */
    scheduler* install_scheduler(std::unique_ptr<scheduler> p_scheduler, telemetry_transport_factory p_make_transport);

} // namespace gempba::detail

#endif // GEMPBA_INSTALL_SCHEDULER_HPP
