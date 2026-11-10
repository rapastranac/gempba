cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 4.4.0 LANGUAGES CXX)

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

    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              ON)
    option(GEMPBA_BUILD_MPI        "Build the MPI component, gempba::mpi" ON)
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

    option(GEMPBA_BUILD_TESTS      "Build gempba unit tests"              OFF)
    option(GEMPBA_BUILD_MPI        "Build the MPI component, gempba::mpi" OFF)
    option(GEMPBA_HWLOC            "Use hwloc for hardware topology"      OFF)
    option(GEMPBA_DEBUG_COMMENTS   "Enable debug comments in gempba"      OFF)

    message(STATUS "GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")

    # ⚠️ DO NOT set CMAKE_BUILD_TYPE here — inherit from parent
    message(STATUS "Using parent project's CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif ()

option(GEMPBA_BUILD_JAVA_BINDING "Build the Java JNI shared library (requires JDK)" OFF)
if (GEMPBA_BUILD_JAVA_BINDING)
    if (NOT DEFINED GEMPBA_JNI_CLASSIFIER OR GEMPBA_JNI_CLASSIFIER STREQUAL "")
        message(FATAL_ERROR
                "GEMPBA_BUILD_JAVA_BINDING requires -DGEMPBA_JNI_CLASSIFIER=<os>-<arch> "
                "(e.g. linux-x86_64, win-x86_64, osx-aarch64)")
    endif ()
endif ()

# 📦 Dependencies
find_package(Threads REQUIRED)
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
message(STATUS "Found spdlog ${spdlog_VERSION} in ${spdlog_DIR}")
message(STATUS "Found fmt ${fmt_VERSION} in ${fmt_DIR}")

if (GEMPBA_BUILD_MPI)
    set(MPI_CXX_SKIP_MPICXX ON)
    find_package(MPI REQUIRED)
endif ()

if (GEMPBA_HWLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
    message(STATUS "Found hwloc ${HWLOC_VERSION}")
endif ()

# 📂 Includes
include_directories(include)
include_directories(private)

# 📦 Subprojects
add_subdirectory(external)

# std::stacktrace lives in libstdc++exp, which some GCC builds, such as Red Hat's gcc-toolset, don't ship.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
check_cxx_source_compiles([=[
    #include <stacktrace>
    int main() { return static_cast<int>(std::stacktrace::current().size()); }
]=] GEMPBA_STACKTRACE_LINKS)
unset(CMAKE_REQUIRED_LIBRARIES)
if (GEMPBA_STACKTRACE_LINKS)
    set(GEMPBA_PC_STACKTRACE_LIBS "-lstdc++exp")
    set(GEMPBA_PC_STACKTRACE_CFLAGS "")
else ()
    set(GEMPBA_PC_STACKTRACE_LIBS "")
    set(GEMPBA_PC_STACKTRACE_CFLAGS "-DGEMPBA_HAS_STACKTRACE=0")
endif ()

# What every target built from gempba's code needs, consumers included: the public headers, the threads library and
# std::stacktrace's library, which node.hpp's inline code uses.
function(gempba_add_public_usage p_target)
    target_include_directories(${p_target} PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${p_target} PUBLIC Threads::Threads)
    if (GEMPBA_STACKTRACE_LINKS)
        target_link_libraries(${p_target} PUBLIC stdc++exp)
    else ()
        target_compile_definitions(${p_target} PUBLIC GEMPBA_HAS_STACKTRACE=0)
    endif ()
    if (UNIX AND NOT APPLE)
        target_link_options(${p_target} PUBLIC -rdynamic)
    endif ()
endfunction()

# How gempba's own sources compile, in the core and in the MPI component. Only what export.h marks leaves a shared library.
function(gempba_configure_objects p_target p_building_define)
    set_target_properties(${p_target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
    )
    target_compile_definitions(${p_target} PRIVATE
            ${p_building_define}
            $<$<BOOL:${GEMPBA_HWLOC}>:GEMPBA_HWLOC>
            $<$<BOOL:${GEMPBA_DEBUG_COMMENTS}>:GEMPBA_DEBUG_COMMENTS>
            $<$<BOOL:${GEMPBA_BUILD_TESTS}>:GEMPBA_BUILD_TESTS>
    )
    target_compile_options(${p_target} PRIVATE
            -pthread -fexceptions
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic -g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
            $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    gempba_add_public_usage(${p_target})
endfunction()

# The public headers use only the standard library (#412). spdlog and fmt are internal and header-only, so the libraries
# carry the code they use, hidden, and the exported targets don't name them. In-tree targets that include private headers
# link gempba_logging.
add_library(gempba_logging INTERFACE)
get_target_property(_spdlog_includes spdlog::spdlog_header_only INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_spdlog_definitions spdlog::spdlog_header_only INTERFACE_COMPILE_DEFINITIONS)
target_include_directories(gempba_logging INTERFACE ${_spdlog_includes})
target_compile_definitions(gempba_logging INTERFACE SPDLOG_HEADER_ONLY ${_spdlog_definitions})
target_link_libraries(gempba_logging INTERFACE fmt::fmt-header-only)

# 📄 Sources: src/mpi/ is the MPI component, the rest of src/ the core.
file(GLOB_RECURSE GEMPBA_CORE_SOURCES "src/*.cpp")
list(FILTER GEMPBA_CORE_SOURCES EXCLUDE REGEX ".*/src/mpi/.*")
file(GLOB_RECURSE GEMPBA_MPI_SOURCES "src/mpi/*.cpp")

# 🛠️ The core's objects, compiled once. libgempba is built from them, and the tests and the JNI library link them
# directly, so they reach the internals libgempba hides.
add_library(gempba_objects OBJECT ${GEMPBA_CORE_SOURCES})
gempba_configure_objects(gempba_objects GEMPBA_BUILDING_CORE)
target_sources(gempba_objects
        PRIVATE FILE_SET cabi_headers TYPE HEADERS
        BASE_DIRS include
        FILES include/gempba/cabi/gempba.h
)
target_include_directories(gempba_objects PRIVATE
        $<TARGET_PROPERTY:gempba_logging,INTERFACE_INCLUDE_DIRECTORIES>
        ${GEMPBA_BS_THREAD_POOL_INCLUDE_DIR}
)
target_compile_definitions(gempba_objects PRIVATE $<TARGET_PROPERTY:gempba_logging,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(gempba_objects PRIVATE fmt::fmt-header-only)

# The JNI library ships alone inside the JAR, so it links hwloc statically (#408).
if (GEMPBA_HWLOC AND GEMPBA_BUILD_JAVA_BINDING)
    find_library(GEMPBA_HWLOC_ARCHIVE NAMES libhwloc.a HINTS ${HWLOC_STATIC_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED)
    set(_hwloc_static_dependencies ${HWLOC_STATIC_LIBRARIES})
    list(REMOVE_ITEM _hwloc_static_dependencies hwloc)
    target_include_directories(gempba_objects PRIVATE ${HWLOC_STATIC_INCLUDE_DIRS})
    target_link_libraries(gempba_objects PRIVATE ${GEMPBA_HWLOC_ARCHIVE} ${_hwloc_static_dependencies} ${HWLOC_STATIC_LDFLAGS_OTHER})
    # hwloc.pc leaves out the frameworks hwloc's macOS backend calls: hwloc passes them only to its own shared library.
    if (APPLE)
        target_link_libraries(gempba_objects PRIVATE "-framework CoreFoundation" "-framework IOKit")
    endif ()
elseif (GEMPBA_HWLOC)
    target_link_libraries(gempba_objects PRIVATE PkgConfig::HWLOC)
endif ()

if (WIN32)
    # process_probe needs psapi (process info); telemetry TCP server needs ws2_32 (Winsock).
    target_link_libraries(gempba_objects PRIVATE psapi ws2_32)
endif ()

# 🛠️ libgempba, the core: a shared library, so spdlog, fmt and hwloc stay inside it.
add_library(gempba SHARED)
target_link_libraries(gempba PRIVATE gempba_objects)
gempba_add_public_usage(gempba)
set_target_properties(gempba PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
add_library(gempba::gempba ALIAS gempba)

# 🛠️ libgempba_mpi, the MPI component, built the same way on top of the core.
if (GEMPBA_BUILD_MPI)
    add_library(gempba_mpi_objects OBJECT ${GEMPBA_MPI_SOURCES})
    gempba_configure_objects(gempba_mpi_objects GEMPBA_BUILDING_MPI)
    target_sources(gempba_mpi_objects
            PRIVATE FILE_SET cabi_headers TYPE HEADERS
            BASE_DIRS include
            FILES include/gempba/cabi/gempba_mpi.h
    )
    target_link_libraries(gempba_mpi_objects PUBLIC MPI::MPI_CXX)

    add_library(gempba_mpi SHARED)
    target_link_libraries(gempba_mpi PRIVATE gempba_mpi_objects PUBLIC gempba MPI::MPI_CXX)
    set_target_properties(gempba_mpi PROPERTIES EXPORT_NAME mpi VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    add_library(gempba::mpi ALIAS gempba_mpi)
endif ()

# On Linux and macOS a shared library also exports the std:: template code it instantiates with spdlog's and fmt's types,
# since the standard library gives its templates default visibility. The export list keeps only gempba's own symbols.
if (APPLE)
    set(GEMPBA_EXPORT_LIST "${PROJECT_SOURCE_DIR}/cmake/gempba.exports")
    set(GEMPBA_EXPORT_LIST_OPTION "LINKER:-exported_symbols_list,${GEMPBA_EXPORT_LIST}")
elseif (UNIX)
    set(GEMPBA_EXPORT_LIST "${PROJECT_SOURCE_DIR}/cmake/gempba.map")
    set(GEMPBA_EXPORT_LIST_OPTION "LINKER:--version-script=${GEMPBA_EXPORT_LIST}")
endif ()
if (GEMPBA_EXPORT_LIST)
    foreach (_gempba_shared IN ITEMS gempba gempba_mpi)
        if (TARGET ${_gempba_shared})
            target_link_options(${_gempba_shared} PRIVATE ${GEMPBA_EXPORT_LIST_OPTION})
            set_property(TARGET ${_gempba_shared} APPEND PROPERTY LINK_DEPENDS ${GEMPBA_EXPORT_LIST})
        endif ()
    endforeach ()
    unset(_gempba_shared)
endif ()

# Print messages to confirm the flags
message(STATUS "🔧 GEMPBA_BUILD_MPI=${GEMPBA_BUILD_MPI}")
message(STATUS "🔧 GEMPBA_HWLOC=${GEMPBA_HWLOC}")
message(STATUS "🔧 GEMPBA_DEBUG_COMMENTS=${GEMPBA_DEBUG_COMMENTS}")
message(STATUS "🔧 GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")

if (GEMPBA_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif ()

# 📦 JNI binding (built only when GEMPBA_BUILD_JAVA_BINDING=ON; the gempba targets must exist first)
if (GEMPBA_BUILD_JAVA_BINDING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bindings/jni/CMakeLists.txt")
    add_subdirectory(bindings/jni)
endif ()

# 📁 Executables output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Generate config.h from config.h.in
configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
        ${CMAKE_CURRENT_BINARY_DIR}/gempba/config.h
)

# ─── Install: the core ───────────────────────────────────────────────────────
install(TARGETS gempba
        EXPORT gempba_targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT core
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT core
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/gempba
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT core
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h"
            PATTERN "mpi" EXCLUDE
            PATTERN "mpi.hpp" EXCLUDE
            PATTERN "gempba_mpi.h" EXCLUDE
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gempba/config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba
        COMPONENT core
)

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
        COMPONENT core
)

install(EXPORT gempba_targets
        FILE gempba-targets.cmake
        NAMESPACE gempba::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
        COMPONENT core
)

configure_file(cmake/gempba.pc.in gempba.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gempba.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT core
)

# ─── Install: the MPI component ──────────────────────────────────────────────
if (GEMPBA_BUILD_MPI)
    install(TARGETS gempba_mpi
            EXPORT gempba_mpi_targets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT mpi
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT mpi
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT mpi
            INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(FILES include/gempba/mpi.hpp
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba
            COMPONENT mpi
    )
    install(DIRECTORY include/gempba/mpi
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba
            COMPONENT mpi
    )
    install(FILES include/gempba/cabi/gempba_mpi.h
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba/cabi
            COMPONENT mpi
    )

    install(EXPORT gempba_mpi_targets
            FILE gempba-mpi-targets.cmake
            NAMESPACE gempba::
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
            COMPONENT mpi
    )

    configure_file(cmake/gempba-mpi.pc.in gempba-mpi.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gempba-mpi.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
            COMPONENT mpi
    )
endif ()

# ─── CPack packaging ─────────────────────────────────────────────────────────
include(cmake/CPackConfig.cmake)
