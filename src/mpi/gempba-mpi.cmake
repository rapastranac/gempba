# find_package(gempba COMPONENTS mpi): gempba::mpi, compiled in the consumer's build from the MPI component's installed
# sources, against the MPI found here (#415). gempbaConfig.cmake includes this file.
set(MPI_CXX_SKIP_MPICXX ON)
find_dependency(MPI REQUIRED)
_gempba_compile_component(mpi MPI::MPI_CXX)
set(gempba_mpi_FOUND TRUE)
