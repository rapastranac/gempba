#include <cabi/internals.hpp>
#include <gempba/cabi/gempba_mpi.h>
#include <gempba/mpi.hpp>
#include <memory>

using gempba::cabi::detail::set_last_error;

// NOLINTBEGIN(readability-identifier-naming)

extern "C" {

    gempba_status_t gempba_mpi_create_scheduler(gempba_scheduler_topology_t topology, gempba_scheduler_t* out) {
        GEMPBA_TRY
            if (out == nullptr) {
                set_last_error("out is null");
                return GEMPBA_ERR_INVALID_ARG;
            }
            auto* sched = gempba::mpi::create_scheduler(static_cast<gempba::mpi::scheduler_topology>(topology));
            auto wrapper = std::make_unique<gempba_scheduler_s>();
            wrapper->cpp = sched;
            *out = wrapper.release();
            return GEMPBA_OK;
        GEMPBA_CATCH_RETURN_STATUS
    }

} // extern "C"

// NOLINTEND(readability-identifier-naming)
