# The MPI component, gempba::mpi: the schedulers that spread a search across MPI ranks. It installs as source, which
# the consumer's build compiles against its own MPI (#415); this build compiles it only for gempba's tests and the JNI
# library.
if (GEMPBA_BUILD_MPI)
    set(MPI_CXX_SKIP_MPICXX ON)
    find_package(MPI REQUIRED)
endif ()

gempba_add_component(mpi
        DEPENDS        gempba::gempba MPI::MPI_CXX
        PUBLIC_HEADERS gempba/mpi.hpp gempba/mpi gempba/cabi/gempba_mpi.h
        COMPILE        ${GEMPBA_BUILD_MPI}
        INSTALL        SOURCE
)

if (GEMPBA_BUILD_MPI)
    target_sources(gempba_mpi_objects
            PRIVATE FILE_SET cabi_headers TYPE HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES ${PROJECT_SOURCE_DIR}/include/gempba/cabi/gempba_mpi.h
    )
endif ()
