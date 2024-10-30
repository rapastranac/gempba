cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

cmake_policy(SET CMP0155 NEW)


find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(MPI REQUIRED)
find_package(Boost COMPONENTS system serialization fiber REQUIRED)
find_package(spdlog REQUIRED)
find_package(GTest REQUIRED)

# Include directories
include_directories(${GTEST_INCLUDE_DIRS})
include_directories(${MPI_INCLUDE_PATH})
include_directories(${Boost_INCLUDE_DIRS})
include_directories("include")
include_directories("GemPBA")

# Enable testing
enable_testing()

# Other project-specific configurations
add_subdirectory("tests")
add_subdirectory("external")
include_directories(${ARGPARSE_INCLUDE_DIR})

# Link directories for Boost
link_directories(${Boost_LIBRARY_DIRS})

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

# Add definitions
add_definitions(-DDEBUG_COMMENTS)


# Ensure to link the correct libraries
file(GLOB_RECURSE Sources "src" "GemPBA/*.cpp")

#sets the executable in main folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Add the main library or executable
add_library(gempba ${Sources})

# Add each example scenario
file(GLOB EXAMPLES "examples/*.cpp")

foreach (EXAMPLE ${EXAMPLES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE) # Get the name without extension
    add_executable(${EXAMPLE_NAME} ${EXAMPLE})

    target_link_libraries(${EXAMPLE_NAME} PRIVATE
            gempba
            Threads::Threads
            MPI::MPI_CXX
            Boost::system
            Boost::serialization
            Boost::fiber
            OpenMP::OpenMP_CXX
            GTest::GTest
            GTest::gmock_main
            GTest::Main
            spdlog::spdlog
    )

    # Check if the example name starts with "mp_"
    if (EXAMPLE_NAME MATCHES "^mp_")
        message(STATUS "Adding MULTIPROCESSING_ENABLED for ${EXAMPLE_NAME}")
        target_compile_definitions(${EXAMPLE_NAME} PUBLIC MULTIPROCESSING_ENABLED)
    endif ()

    # If MPI compile flags exist, apply them
    if (MPI_COMPILE_FLAGS)
        set_target_properties(${EXAMPLE_NAME} PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
    endif ()
endforeach ()