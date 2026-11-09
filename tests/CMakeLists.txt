cmake_minimum_required(VERSION 3.28)

project(gempba_tests LANGUAGES CXX)

# We need GTest and Boost
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()
find_package(Boost COMPONENTS serialization fiber REQUIRED)
find_package(GTest REQUIRED)

# Include GoogleTest module for test discovery
include(GoogleTest)

# Include directories
include_directories(support)
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${CMAKE_SOURCE_DIR}/private)
include_directories(${GEMPBA_BS_THREAD_POOL_INCLUDE_DIR})

# Set the output directory to be the same as the main project
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

enable_testing()

# Set up the main test directory
set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Collect all test source files recursively
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")
list(FILTER TEST_SOURCES EXCLUDE REGEX "/integration/")

# Ensure that source files are found
if (NOT TEST_SOURCES)
    message(WARNING "No test source files found in ${TEST_DIR}, skipping test executable.\n")
    return()
endif ()

# Create a single test executable
add_executable(all_tests ${TEST_SOURCES})

# Link necessary libraries
target_link_libraries(all_tests
        PRIVATE
        gempba
        gempba_logging
        GTest::gtest
        GTest::gmock_main
        GTest::gtest_main
        Boost::serialization
        Boost::fiber
)

if (WIN32)
    # Telemetry tests (TCP server / local transport) talk Winsock directly.
    target_link_libraries(all_tests PRIVATE ws2_32)
endif ()

# Compilation options by mode
target_compile_options(all_tests PRIVATE
        -Wall -Wextra -Wpedantic -pthread -fexceptions
        $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# 🔥 CRITICAL: Discover individual GTest tests and register them with CTest
# This allows CTest to see each test case individually
gtest_discover_tests(all_tests
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        PROPERTIES
        TIMEOUT 60
        DISCOVERY_TIMEOUT 30
)

message(STATUS "GTest test discovery enabled for all_tests executable")

option(GEMPBA_MPI_INTEGRATION_TESTS "Register multi-rank MPI tests with CTest (runs mpiexec)" OFF)
if (GEMPBA_MPI_INTEGRATION_TESTS)
    add_executable(termination_stress integration/termination_stress.cpp)
    target_link_libraries(termination_stress PRIVATE gempba)
    foreach (v_topology semi_centralized centralized)
        foreach (v_ranks 2 4 8)
            set(v_test_name mpi_termination_stress_${v_topology}_${v_ranks})
            add_test(NAME ${v_test_name}
                    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${v_ranks} ${MPIEXEC_PREFLAGS} $<TARGET_FILE:termination_stress> ${MPIEXEC_POSTFLAGS} 14 ${v_topology})
            set_tests_properties(${v_test_name} PROPERTIES TIMEOUT 300)
        endforeach ()
    endforeach ()

    add_executable(telemetry_stress integration/telemetry_stress.cpp)
    target_link_libraries(telemetry_stress PRIVATE gempba)
    if (WIN32)
        target_link_libraries(telemetry_stress PRIVATE ws2_32)
    endif ()
    foreach (v_topology semi_centralized centralized)
        set(v_test_name mpi_telemetry_stress_${v_topology}_4)
        add_test(NAME ${v_test_name}
                COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 4 ${MPIEXEC_PREFLAGS} $<TARGET_FILE:telemetry_stress> ${MPIEXEC_POSTFLAGS} 14 ${v_topology})
        set_tests_properties(${v_test_name} PROPERTIES TIMEOUT 300)
    endforeach ()
    message(STATUS "MPI integration tests registered (mpiexec: ${MPIEXEC_EXECUTABLE})")
endif ()