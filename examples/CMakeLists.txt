cmake_minimum_required(VERSION 3.28)

project(gempba_examples LANGUAGES CXX)

cmake_policy(SET CMP0167 NEW)

# Static linking of Boost
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost COMPONENTS system serialization fiber REQUIRED)

# Include Boost y argparse
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${ARGPARSE_INCLUDE_DIR})

# Add each example scenario
file(GLOB EXAMPLES "*.cpp")

foreach (EXAMPLE ${EXAMPLES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE) # Get the name without extension
    add_executable(${EXAMPLE_NAME} ${EXAMPLE})

    # Link examples with GemPBA and Boost
    target_link_libraries(${EXAMPLE_NAME}
            PRIVATE
            gempba
            Boost::system
            Boost::serialization
            Boost::fiber
    )

    # Executables output directory
    set_target_properties(${EXAMPLE_NAME}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )

    # Check if the example name starts with "mp_"
    if (EXAMPLE_NAME MATCHES "^mp_")
        message(STATUS "Adding GEMPBA_MULTIPROCESSING for ${EXAMPLE_NAME}")
        target_compile_definitions(${EXAMPLE_NAME} PRIVATE GEMPBA_MULTIPROCESSING)
    endif ()

    # Compilation options by mode
    target_compile_options(${EXAMPLE_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -pthread -fexceptions -fconcepts
            $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
            $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endforeach ()