# termination_stress built against an installed gempba, so gempba::mpi compiles from the MPI component's installed sources
# against this machine's MPI (#415). Configure with CMAKE_PREFIX_PATH pointing at the installation.
cmake_minimum_required(VERSION 3.28)
project(gempba_install_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gempba REQUIRED COMPONENTS mpi)

add_executable(termination_stress ../integration/termination_stress.cpp)
target_link_libraries(termination_stress PRIVATE gempba::mpi)
