cmake_minimum_required(VERSION 3.28)

project(gempba_examples LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# 📦 Subprojects
add_subdirectory(external)

# Static linking of Boost
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost COMPONENTS serialization fiber REQUIRED)

# Include Boost y argparse
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${ARGPARSE_INCLUDE_DIR})

# Add each example scenario. mp_* examples consume gempba::multiprocessing::** and require
# GEMPBA_MULTIPROCESSING=ON; skip them in MT-only builds so the example tree
# stays buildable without MPI on the system.
file(GLOB EXAMPLES "*.cpp")
if (NOT GEMPBA_MULTIPROCESSING)
    list(FILTER EXAMPLES EXCLUDE REGEX ".*/mp_[^/]+\\.cpp$")
endif ()

foreach (EXAMPLE ${EXAMPLES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE) # Get the name without extension
    add_executable(${EXAMPLE_NAME} ${EXAMPLE})

    # GEMPBA_MULTIPROCESSING is no longer set per-example; examples inherit the
    # macro from the gempba target's PUBLIC compile definition.

    # Link examples with GemPBA and Boost
    target_link_libraries(${EXAMPLE_NAME}
            PRIVATE
            gempba
            example_libs
            Boost::serialization
            Boost::fiber
    )

    # Executables output directory
    set_target_properties(${EXAMPLE_NAME}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )

    # Compilation options by mode
    target_compile_options(${EXAMPLE_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -pthread -fexceptions
            $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
            $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endforeach ()