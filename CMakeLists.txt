cmake_minimum_required(VERSION 3.28)

project(gempba VERSION 4.4.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(cmake/gempba_settings.cmake)     # options and build-wide settings
include(cmake/gempba_component.cmake)    # gempba_add_component()

add_subdirectory(src/core)               # gempba::gempba, the core
add_subdirectory(src/mpi)                # gempba::mpi, installed as source

if (GEMPBA_BUILD_TESTS AND EXISTS "${PROJECT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif ()

if (GEMPBA_BUILD_JAVA_BINDING AND EXISTS "${PROJECT_SOURCE_DIR}/bindings/jni/CMakeLists.txt")
    add_subdirectory(bindings/jni)       # the Java binding's native library
endif ()

include(cmake/gempba_install.cmake)      # public headers, find_package and pkg-config files, packages
