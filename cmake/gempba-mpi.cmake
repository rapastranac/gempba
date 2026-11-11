# gempba::mpi from the MPI component's installed sources, compiled in the consumer's build against the MPI it found
# (#415). gempbaConfig.cmake sets GEMPBA_MPI_SOURCE_DIR and finds MPI before including this file.
if (NOT TARGET gempba::mpi)
    file(GLOB_RECURSE _gempba_mpi_sources "${GEMPBA_MPI_SOURCE_DIR}/src/*.cpp")
    add_library(gempba_mpi STATIC ${_gempba_mpi_sources})
    target_include_directories(gempba_mpi PRIVATE "${GEMPBA_MPI_SOURCE_DIR}/private")
    target_link_libraries(gempba_mpi PUBLIC gempba::gempba MPI::MPI_CXX)
    target_compile_features(gempba_mpi PUBLIC cxx_std_23)
    set_target_properties(gempba_mpi PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_SCAN_FOR_MODULES OFF
    )
    add_library(gempba::mpi ALIAS gempba_mpi)
    unset(_gempba_mpi_sources)
endif ()
