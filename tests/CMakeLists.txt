cmake_minimum_required(VERSION 3.28)

project(gempba_tests)

set(CMAKE_CXX_STANDARD 23)

# Set the output directory to be the same as the main project
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

# Set up the main test directory
set(TEST_DIR "${CMAKE_SOURCE_DIR}/tests")

add_definitions(-DMULTIPROCESSING_ENABLED)

message("Tests source dir: ${TEST_DIR}\n")

# Compiler and linker options
add_compile_options(
        -std=c++23
        -fconcepts
        -O0         # 0 - for debugging purpose, 3 - for release
        -pthread
        -Wall
        -Wpedantic
        -Wextra
        -fexceptions
        -g          # for debugging purpose
        -ggdb3      # for debugging purpose
)


# Collect all test source files recursively
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")

# Ensure that source files are found
if (NOT TEST_SOURCES)
    message(FATAL_ERROR "No test source files found in ${TEST_DIR}\n")
endif ()

# Create a single test executable
add_executable(all_tests.out ${TEST_SOURCES})
message("Should have created an exec: ${TEST_SOURCES}\n")

# Include necessary directories
target_include_directories(all_tests.out PRIVATE "${TEST_DIR}")

# Link necessary libraries (e.g., Google Test)
target_link_libraries(all_tests.out
        PRIVATE
        Boost::system
        Boost::serialization
        Boost::fiber
        GTest::gtest
        GTest::gmock_main
        GTest::gtest_main
        spdlog::spdlog
        ${MPI_LIBRARIES}
        OpenMP::OpenMP_CXX
)

# Add the test to the testing system
add_test(NAME all_tests.out COMMAND all_tests.out)
