cmake_minimum_required(VERSION 3.28)

project(gempba_tests LANGUAGES CXX)

# We need GTest and Boost
cmake_policy(SET CMP0167 NEW)
find_package(Boost COMPONENTS system serialization fiber REQUIRED)
find_package(GTest REQUIRED)

# Include GoogleTest module for test discovery
include(GoogleTest)

# Include directories
include_directories(support)
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
add_executable(all_tests ${TEST_SOURCES})

# Link necessary libraries
target_link_libraries(all_tests
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
target_compile_options(all_tests PRIVATE
        -Wall -Wextra -Wpedantic -pthread -fexceptions -fconcepts
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