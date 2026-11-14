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
 * gempba C ABI — the MPI component, in libgempba_mpi.
 *
 * Adds what needs MPI to the calls gempba.h declares: creating one of the built-in MPI schedulers.
 */

#ifndef GEMPBA_CABI_GEMPBA_MPI_H
#define GEMPBA_CABI_GEMPBA_MPI_H

#include <gempba/cabi/gempba.h>
#include <gempba/export.h>

#ifdef __cplusplus
extern "C" {
#endif

    // NOLINTBEGIN(readability-identifier-naming)

    /** Mirrors gempba::mpi::scheduler_topology. */
    typedef enum {
        GEMPBA_TOPOLOGY_SEMI_CENTRALIZED = 0,
        GEMPBA_TOPOLOGY_CENTRALIZED      = 1
    } gempba_scheduler_topology_t;

    /**
     * Creates the process-wide scheduler with the given topology.  Wraps gempba::mpi::create_scheduler.  MPI must
     * already be initialised; the scheduler does not own the MPI lifecycle.
     */
    GEMPBA_MPI_API gempba_status_t gempba_mpi_create_scheduler(gempba_scheduler_topology_t topology,
                                                               gempba_scheduler_t*         out);

    // NOLINTEND(readability-identifier-naming)

#ifdef __cplusplus
}
#endif

#endif /* GEMPBA_CABI_GEMPBA_MPI_H */
