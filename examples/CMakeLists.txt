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

# Each mode has its own subtree (multithreading/{src,include}, multiprocessing/
# {src,include}) and its own CMakeLists that drives its example targets. The
# multiprocessing/ subtree consumes gempba::multiprocessing::* and is only
# added when GEMPBA_MULTIPROCESSING=ON. The mp_*.cpp files still at the root
# move into the multiprocessing/ subtree in a later commit.

add_subdirectory(multithreading)

# Pre-migration: mp_*.cpp examples still live at the root until their
# structural move lands. The legacy filter keeps them out of MT-only builds.
file(GLOB ROOT_EXAMPLES "*.cpp")
if (NOT GEMPBA_MULTIPROCESSING)
    list(FILTER ROOT_EXAMPLES EXCLUDE REGEX ".*/mp_[^/]+\\.cpp$")
endif ()

foreach (EXAMPLE ${ROOT_EXAMPLES})
    get_filename_component(EXAMPLE_NAME ${EXAMPLE} NAME_WE)
    add_executable(${EXAMPLE_NAME} ${EXAMPLE})

    target_link_libraries(${EXAMPLE_NAME}
            PRIVATE
            gempba
            example_libs
            Boost::serialization
            Boost::fiber
    )

    set_target_properties(${EXAMPLE_NAME}
            PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
    )

    target_compile_options(${EXAMPLE_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -pthread -fexceptions
            $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
            $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
endforeach ()