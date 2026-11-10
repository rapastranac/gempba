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
#ifndef GEMPBA_MPI_HPP
#define GEMPBA_MPI_HPP

#include <gempba/core/scheduler.hpp>
#include <gempba/export.h>
#include <gempba/mpi/default_mpi_stats_visitor.hpp>
#include <memory>

/** @brief The MPI component, @c gempba::mpi in CMake: the schedulers that run a search across MPI ranks. */
namespace gempba::mpi {

    /** @brief The built-in MPI schedulers. */
    enum scheduler_topology {
        SEMI_CENTRALIZED,
        CENTRALIZED,
    };

    /**
     * @brief Create the built-in MPI scheduler for @p p_topology and install telemetry over MPI. Every rank calls it.
     *
     * @throws std::runtime_error when a scheduler already exists.
     */
    GEMPBA_MPI_API scheduler* create_scheduler(const scheduler_topology& p_topology);

    /**
     * @brief Install your own scheduler, which runs on MPI, and telemetry over MPI. Every rank calls it.
     *
     * @throws std::runtime_error when a scheduler already exists.
     */
    GEMPBA_MPI_API scheduler* create_scheduler(std::unique_ptr<scheduler> p_your_implementation);

    /** @brief A visitor that reads the built-in MPI schedulers' statistics. */
    GEMPBA_MPI_API std::unique_ptr<default_mpi_stats_visitor> get_default_mpi_stats_visitor();

} // namespace gempba::mpi

#endif // GEMPBA_MPI_HPP
