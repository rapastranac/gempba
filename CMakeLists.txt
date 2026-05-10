cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 3.3.0 LANGUAGES CXX)

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
    message(STATUS "GemPBA detected as root project. Defaulting to build tests.")

    option(GEMPBA_DEV_MODE         "Enable dev mode"                      ON)
    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              ON)
    option(GEMPBA_MULTIPROCESSING  "Enable multiprocessing support"       ON)
    option(GEMPBA_HWLOC            "Use hwloc for hardware topology"      ON)
    option(GEMPBA_DEBUG_COMMENTS   "Enable debug comments in gempba"      OFF)

    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")

    # 🎯 If we are developing gempba and the type was not specified, use Debug
    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default build type for GemPBA")
        message(STATUS "No build type specified. Defaulting to Debug.")
    else ()
        message(STATUS "Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif ()

else ()
    message(STATUS "GemPBA detected as a subproject. Defaulting to not build tests.")

    option(GEMPBA_DEV_MODE         "Enable dev mode"                      OFF)
    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              OFF)
    option(GEMPBA_MULTIPROCESSING  "Enable multiprocessing support"       OFF)
    option(GEMPBA_HWLOC            "Use hwloc for hardware topology"      OFF)
    option(GEMPBA_DEBUG_COMMENTS   "Enable debug comments in gempba"      OFF)

    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")

    # ⚠️ DO NOT set CMAKE_BUILD_TYPE here — inherit from parent
    message(STATUS "Using parent project's CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif ()

# 📦 Dependencies
find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)

if (GEMPBA_MULTIPROCESSING)
    find_package(MPI REQUIRED)
endif ()

if (GEMPBA_HWLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
    message(STATUS "Found hwloc ${HWLOC_VERSION}")
endif ()

# 📂 Includes
if (GEMPBA_MULTIPROCESSING)
    include_directories(${MPI_INCLUDE_PATH})
endif ()
include_directories(include)
include_directories(private)

# 📦 Subprojects
add_subdirectory(external)

if (GEMPBA_BUILD_TESTS AND GEMPBA_MULTIPROCESSING AND GEMPBA_DEV_MODE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
elseif (GEMPBA_BUILD_TESTS AND NOT GEMPBA_MULTIPROCESSING)
    message(STATUS "Skipping tests: tests reference gempba::multiprocessing::* and require GEMPBA_MULTIPROCESSING=ON")
elseif (GEMPBA_BUILD_TESTS AND NOT GEMPBA_DEV_MODE)
    message(STATUS "Skipping tests: tests use the explicit gempba::multithreading::* form and require GEMPBA_DEV_MODE=ON")
endif ()

# 📄 Main source files
file(GLOB_RECURSE Sources "src/*.cpp")
if (NOT GEMPBA_MULTIPROCESSING)
    list(FILTER Sources EXCLUDE REGEX ".*/src/schedulers/mpi/.*")
    list(FILTER Sources EXCLUDE REGEX ".*/src/telemetry/mpi_transport\\.cpp$")
endif ()

# 🛠️ Create the main library
add_library(gempba STATIC ${Sources})

if (GEMPBA_MULTIPROCESSING)
    set(GEMPBA_FLAVOR_TAG mpi)
    set_target_properties(gempba PROPERTIES OUTPUT_NAME gempba_mpi)
else ()
    set(GEMPBA_FLAVOR_TAG mt)
endif ()

# 🔧️ Add macros for the library
# GEMPBA_MULTIPROCESSING is PUBLIC so consumers (and examples) receive the
# same flag the library was built with via target propagation.
target_compile_definitions(gempba
        PUBLIC
            GEMPBA_MULTIPROCESSING=$<IF:$<BOOL:${GEMPBA_MULTIPROCESSING}>,1,0>
        PRIVATE
            $<$<BOOL:${GEMPBA_DEV_MODE}>:GEMPBA_DEV_MODE>
            $<$<BOOL:${GEMPBA_HWLOC}>:GEMPBA_HWLOC>
            $<$<BOOL:${GEMPBA_DEBUG_COMMENTS}>:GEMPBA_DEBUG_COMMENTS>
            $<$<BOOL:${GEMPBA_BUILD_TESTS}>:GEMPBA_BUILD_TESTS>
)

# Print messages to confirm the flags
message(STATUS "🔧 GEMPBA_DEV_MODE=${GEMPBA_DEV_MODE}")
message(STATUS "🔧 GEMPBA_MULTIPROCESSING=${GEMPBA_MULTIPROCESSING}")
message(STATUS "🔧 GEMPBA_HWLOC=${GEMPBA_HWLOC}")
message(STATUS "🔧 GEMPBA_DEBUG_COMMENTS=${GEMPBA_DEBUG_COMMENTS}")
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
        OpenMP::OpenMP_CXX
        fmt::fmt
)

if (GEMPBA_MULTIPROCESSING)
    target_link_libraries(gempba PUBLIC MPI::MPI_CXX)
endif ()

# spdlog is used in header-only mode. Deliberately NOT linked as a CMake target
# so spdlog::spdlog_header_only never appears in the exported targets file and
# consumers don't need find_package(spdlog) just to use gempba.
get_target_property(_spdlog_includes spdlog::spdlog_header_only INTERFACE_INCLUDE_DIRECTORIES)
target_include_directories(gempba PRIVATE ${_spdlog_includes})
target_compile_definitions(gempba PUBLIC SPDLOG_HEADER_ONLY)

# C++23 stacktrace support (standard library, not external)
if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14)
    target_link_libraries(gempba PUBLIC stdc++exp)
endif ()

if (GEMPBA_HWLOC)
    target_link_libraries(gempba PRIVATE PkgConfig::HWLOC)
endif ()

if (WIN32)
    # process_probe needs psapi (process info); telemetry TCP server needs ws2_32 (Winsock).
    target_link_libraries(gempba PRIVATE psapi ws2_32)
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
        EXPORT gempba_${GEMPBA_FLAVOR_TAG}_Targets
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
install(EXPORT gempba_${GEMPBA_FLAVOR_TAG}_Targets
        FILE gempba_${GEMPBA_FLAVOR_TAG}-targets.cmake
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