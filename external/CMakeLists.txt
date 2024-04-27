cmake_minimum_required(VERSION 3.28)
include(FetchContent)

project(external)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_SOURCE_DIR}/external CACHE PATH "External install location" FORCE)

#region FMT
# Set the installation location for fmt
set(FMT_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "Fmt install location" FORCE)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG e69e5f977d458f2650bb346dadf2ad30c5320281    # release-10.2.1
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
        GIT_TAG af442b4da0cd7a07b56fa709bd16571889dc7fda    # v3.0
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



FetchContent_MakeAvailable(fmt argparse)
