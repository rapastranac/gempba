cmake_minimum_required(VERSION 3.28)

project(gempba_tests LANGUAGES CXX)

# We need GTest y Boost
cmake_policy(SET CMP0167 NEW)
find_package(Boost COMPONENTS system serialization fiber REQUIRED)
find_package(GTest REQUIRED)

# Include directories
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${Boost_INCLUDE_DIRS})

# Set the output directory to be the same as the main project
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

enable_testing()

# Set up the main test directory
set(TEST_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Collect all test source files recursively
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "${TEST_DIR}/*.cpp")

# Ensure that source files are found
if (NOT TEST_SOURCES)
    message(WARNING "No test source files found in ${TEST_DIR}, skipping test executable.\n")
    return()
endif ()

# Create a single test executable
add_executable(all_tests.out ${TEST_SOURCES})

# Link necessary libraries (e.g., Google Test)
target_link_libraries(all_tests.out
        PRIVATE
        gempba
        GTest::gtest
        GTest::gmock_main
        GTest::gtest_main
        Boost::system
        Boost::serialization
        Boost::fiber
)

# Compilation options by mode
target_compile_options(all_tests.out PRIVATE
        -Wall -Wextra -Wpedantic -pthread -fexceptions -fconcepts
        $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# Add the test to the testing system
add_test(NAME all_tests.out COMMAND all_tests.out)
