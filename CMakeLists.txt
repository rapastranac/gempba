cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 3.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 🔍 DYNAMIC SYMBOL EXPORT - ONLY FOR DEBUG BUILDS, LINUX-ONLY
if (NOT MSVC AND UNIX)
    add_link_options($<$<CONFIG:Debug>:-rdynamic>)
endif ()

# ⚙️ Options — declared inside the if/else so defaults are context-aware.
# option() only writes the cache when the key is not yet present, so user-passed
# -DGEMPBA_BUILD_TESTS=OFF (or =ON) always takes precedence over these defaults.
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    message(STATUS "GemPBA detected as root project. Defaulting to build tests and examples.")

    option(GEMPBA_DEV_MODE         "Enable dev mode"                      ON)
    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              ON)
    option(GEMPBA_BUILD_EXAMPLES   "Build gempba examples"                ON)
    option(GEMPBA_MULTIPROCESSING  "Enable multiprocessing support"       ON)
    option(GEMPBA_DEBUG_COMMENTS   "Enable debug comments in gempba"      OFF)

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

    option(GEMPBA_DEV_MODE         "Enable dev mode"                      OFF)
    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              OFF)
    option(GEMPBA_BUILD_EXAMPLES   "Build gempba examples"                OFF)
    option(GEMPBA_MULTIPROCESSING  "Enable multiprocessing support"       OFF)
    option(GEMPBA_DEBUG_COMMENTS   "Enable debug comments in gempba"      OFF)

    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}, GEMPBA_BUILD_EXAMPLES=${GEMPBA_BUILD_EXAMPLES}")

    # ⚠️ DO NOT set CMAKE_BUILD_TYPE here — inherit from parent
    message(STATUS "Using parent project's CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif ()

# 📦 Dependencies
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(MPI REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

# 📂 Includes
include_directories(${MPI_INCLUDE_PATH})
include_directories(include)
include_directories(private)

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
file(GLOB_RECURSE Sources "src/*.cpp")

# 🛠️ Create the main library
add_library(gempba STATIC ${Sources})

# 🔧️ Add macros for the library
target_compile_definitions(gempba PRIVATE
        $<$<BOOL:${GEMPBA_DEV_MODE}>:GEMPBA_DEV_MODE>
        $<$<BOOL:${GEMPBA_MULTIPROCESSING}>:GEMPBA_MULTIPROCESSING>
        $<$<BOOL:${GEMPBA_DEBUG_COMMENTS}>:GEMPBA_DEBUG_COMMENTS>
        $<$<BOOL:${GEMPBA_BUILD_EXAMPLES}>:GEMPBA_BUILD_EXAMPLES>
        $<$<BOOL:${GEMPBA_BUILD_TESTS}>:GEMPBA_BUILD_TESTS>
)

# Print messages to confirm the flags
message(STATUS "🔧 GEMPBA_DEV_MODE=${GEMPBA_DEV_MODE}")
message(STATUS "🔧 GEMPBA_MULTIPROCESSING=${GEMPBA_MULTIPROCESSING}")
message(STATUS "🔧 GEMPBA_DEBUG_COMMENTS=${GEMPBA_DEBUG_COMMENTS}")
message(STATUS "🔧 GEMPBA_BUILD_EXAMPLES=${GEMPBA_BUILD_EXAMPLES}")
message(STATUS "🔧 GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")


# 🏷️ Conditional compilation flags for Debug/Release
target_compile_options(gempba PRIVATE
        -pthread -fexceptions
        $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic -g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
)

# 🔗 Linking
# external_libs is intentionally excluded: it is not an installable exported target.
# Its contents (BS_thread_pool headers, spdlog) are wired in via target_include_directories.
target_link_libraries(gempba PUBLIC
        Threads::Threads
        MPI::MPI_CXX
        OpenMP::OpenMP_CXX
        fmt::fmt
)

# spdlog is used in header-only mode. Deliberately NOT linked as a CMake target
# so spdlog::spdlog_header_only never appears in gempbaTargets.cmake and
# consumers don't need find_package(spdlog) just to use gempba.
get_target_property(_spdlog_includes spdlog::spdlog_header_only INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(gempba PRIVATE ${_spdlog_includes})
target_compile_definitions(gempba PUBLIC SPDLOG_HEADER_ONLY)

# C++23 stacktrace support (standard library, not external)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(gempba PUBLIC stdc++exp)
endif ()

if (UNIX AND NOT APPLE)
    target_link_options(gempba PUBLIC -rdynamic)
endif ()

# 📁 Executables output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# modern alias for the library
add_library(gempba::gempba ALIAS gempba)

# Generate config.h from config.h.in
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/gempba/config.h
)

# 📤 Includes for consumers of the library
# - BS_thread_pool: PRIVATE, only used in private impl headers.
target_include_directories(gempba
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${GEMPBA_BS_THREAD_POOL_INCLUDE_DIR}
)


# ─── Install: library archive ────────────────────────────────────────────────
install(TARGETS gempba
        EXPORT gempbaTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# ─── Install: public headers ──────────────────────────────────────────────────
install(DIRECTORY include/gempba
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
)

# ─── Install: generated config.h ─────────────────────────────────────────────
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gempba/config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba
)

# ─── Install: exported CMake targets ─────────────────────────────────────────
install(EXPORT gempbaTargets
        FILE gempbaTargets.cmake
        NAMESPACE gempba::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
)

# ─── Generate and install: CMake package config files ────────────────────────
configure_package_config_file(
        cmake/gempbaConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/gempbaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/gempbaConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/gempbaConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/gempbaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
)

# ─── Generate and install: pkg-config .pc file ───────────────────────────────
configure_file(cmake/gempba.pc.in gempba.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gempba.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# ─── CPack packaging ─────────────────────────────────────────────────────────
include(cmake/CPackConfig.cmake)