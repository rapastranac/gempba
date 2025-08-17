cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 2.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)

# ⚙️ Flags to control the compilation of tests and examples
option(GEMPBA_BUILD_TESTS "Build gempba unit tests" OFF)
option(GEMPBA_BUILD_EXAMPLES "Build gempba examples" OFF)
option(GEMPBA_MULTIPROCESSING "Enable multiprocessing support in gempba" OFF)
option(GEMPBA_DEBUG_COMMENTS "Enable debug comments in gempba" OFF)

# 🧠 If used as a root project, we default to building tests and examples, otherwise, we turn them off.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    message(STATUS "GemPBA detected as root project. Defaulting to build tests and examples.")

    set(GEMPBA_MULTIPROCESSING ON)
    set(GEMPBA_BUILD_TESTS ON)
    set(GEMPBA_BUILD_EXAMPLES ON)

    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}, GEMPBA_BUILD_EXAMPLES=${GEMPBA_BUILD_EXAMPLES}")

    # 🎯 If we are developing gempba and the type was not specified, use Debug
    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default build type for GemPBA")
        message(STATUS "No build type specified. Defaulting to Debug.")
    else ()
        message(STATUS "Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif ()

else ()
    message(STATUS "GemPBA detected as a subproject. Defaulting to not build tests and examples.")
    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}, GEMPBA_BUILD_EXAMPLES=${GEMPBA_BUILD_EXAMPLES}")

    # ⚠️ DO NOT set CMAKE_BUILD_TYPE here, to let it inherit from the parent
    # Just print what the parent set
    message(STATUS "Using parent project's CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif ()

# 📦 Dependencies
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(MPI REQUIRED)

# 📂 Includes
include_directories(${MPI_INCLUDE_PATH})
include_directories(include)
include_directories(GemPBA)

# 📦 Subprojects
add_subdirectory(external)

if (GEMPBA_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif ()

if (GEMPBA_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif ()

# 📄 Main source files
file(GLOB_RECURSE Sources "src/*.cpp" "GemPBA/*.cpp")

# 🛠️ Create the main library
add_library(gempba STATIC ${Sources})

# 🔧️ Add macros for the library
target_compile_definitions(gempba PUBLIC
        $<$<BOOL:${GEMPBA_MULTIPROCESSING}>:GEMPBA_MULTIPROCESSING>
        $<$<BOOL:${GEMPBA_DEBUG_COMMENTS}>:GEMPBA_DEBUG_COMMENTS>
        $<$<BOOL:${GEMPBA_BUILD_EXAMPLES}>:GEMPBA_BUILD_EXAMPLES>
        $<$<BOOL:${GEMPBA_BUILD_TESTS}>:GEMPBA_BUILD_TESTS>
)

# Print messages to confirm the flags
message(STATUS "🔧 GEMPBA_MULTIPROCESSING=${GEMPBA_MULTIPROCESSING}")
message(STATUS "🔧 GEMPBA_DEBUG_COMMENTS=${GEMPBA_DEBUG_COMMENTS}")
message(STATUS "🔧 GEMPBA_BUILD_EXAMPLES=${GEMPBA_BUILD_EXAMPLES}")
message(STATUS "🔧 GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")



# 🏷️ Conditional compilation flags for Debug/Release
target_compile_options(gempba PRIVATE
        -Wall -Wextra -Wpedantic -pthread -fexceptions -fconcepts
        $<$<CONFIG:Debug>:-g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# 🔗 Linking
target_link_libraries(gempba PUBLIC
        external_libs
        Threads::Threads
        MPI::MPI_CXX
        OpenMP::OpenMP_CXX
)

# 📁 Executables output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# modern alias for the library
add_library(gempba::gempba ALIAS gempba)

# 📤 Includes for consumers of the library
target_include_directories(gempba
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/GemPBA>
)