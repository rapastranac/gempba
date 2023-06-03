cmake_minimum_required(VERSION 3.2)
include(FetchContent)

project(external)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_SOURCE_DIR}/external CACHE PATH "External install location" FORCE)

#region FMT

# Set the installation location for fmt
set(FMT_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "Fmt install location" FORCE)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG a0b8a92e3d1532361c2f7feb63babc5c18d00ef2    # release-10.0.0
        SOURCE_DIR ${FMT_INSTALL_LOCATION}/fmt
)

FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
    # Fetch the content using previously declared details
    FetchContent_Populate(fmt)

    # Set custom variables, policies, etc.
    # ...

    # Bring the populated content into the build
    add_subdirectory(${fmt_SOURCE_DIR} ${fmt_BINARY_DIR})
endif()

# Define a custom variable for the include directory
set(FMT_INCLUDE_DIR ${FMT_INSTALL_LOCATION}/fmt/include CACHE PATH "fmt include directory")

# Install targets for the external libraries
install(DIRECTORY ${FMT_INSTALL_LOCATION}/fmt/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/fmt)

#endregion

#region ARGPARSE

# Set the installation location for fmt
set(ARGPARSE_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "argparse install location" FORCE)

FetchContent_Declare(
        argparse
        GIT_REPOSITORY https://github.com/p-ranav/argparse.git
        GIT_TAG 997da9255618311d1fcb0135ce86022729d1f1cb    # v2.9
        SOURCE_DIR ${ARGPARSE_INSTALL_LOCATION}/argparse
)

FetchContent_GetProperties(argparse)
if(NOT argparse_POPULATED)
    # Fetch the content using previously declared details
    FetchContent_Populate(argparse)

    # Set custom variables, policies, etc.
    # ...

    # Bring the populated content into the build
    add_subdirectory(${argparse_SOURCE_DIR} ${argparse_BINARY_DIR})
endif()

# Define a custom variable for the include directory
set(ARGPARSE_INCLUDE_DIR ${ARGPARSE_INSTALL_LOCATION}/argparse/include CACHE PATH "argparse include directory")

# Install targets for the external libraries
install(DIRECTORY ${ARGPARSE_INSTALL_LOCATION}/argparse/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/argparse)

#endregion

#region GTEST

# Set the installation location for gtest
set(GTEST_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "GTest install location" FORCE)

FetchContent_Declare(
        gtest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG b796f7d44681514f58a683a3a71ff17c94edb0c1    # v1.13.0
        SOURCE_DIR ${GTEST_INSTALL_LOCATION}/gtest
        BINARY_DIR ${GTEST_INSTALL_LOCATION}/gtest/build  # Specify the build directory
)

FetchContent_GetProperties(gtest)
if(NOT gtest_POPULATED)
    # Fetch the content using previously declared details
    FetchContent_Populate(gtest)

    # Set custom variables, policies, etc.
    # ...

    # Bring the populated content into the build
    #    add_subdirectory(${gtest_SOURCE_DIR} ${gtest_BINARY_DIR})
    add_subdirectory(${gtest_SOURCE_DIR} ${GTEST_INSTALL_LOCATION}/gtest/build)
endif()


# Define a custom variable for the include directory of Google Test
set(GTEST_INCLUDE_DIR ${googletest_SOURCE_DIR}/googletest/include CACHE PATH "Google Test include directory")

# Define a custom variable for the library directory of Google Test
set(GTEST_LIB_DIR ${googletest_BINARY_DIR}/lib CACHE PATH "Google Test library directory")

# Install targets for the external libraries
install(DIRECTORY ${GTEST_INSTALL_LOCATION}/gtest/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/gtest)

#endregion

FetchContent_MakeAvailable(fmt argparse gtest)
