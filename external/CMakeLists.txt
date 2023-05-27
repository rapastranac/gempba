cmake_minimum_required(VERSION 3.22)
project(external)


include(FetchContent)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_SOURCE_DIR}/external CACHE PATH "External install location" FORCE)

FetchContent_Declare(
        fmt
        GIT_REPOSITORY https://github.com/fmtlib/fmt.git
        GIT_TAG a0b8a92e3d1532361c2f7feb63babc5c18d00ef2    # release-10.0.0
        SOURCE_DIR ${EXTERNAL_INSTALL_LOCATION}/fmt
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
set(FMT_INCLUDE_DIR ${EXTERNAL_INSTALL_LOCATION}/fmt/include CACHE PATH "fmt include directory")

# Install targets for the external libraries
install(DIRECTORY ${EXTERNAL_INSTALL_LOCATION}/fmt/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/fmt)

FetchContent_Declare(
        argparse
        GIT_REPOSITORY https://github.com/p-ranav/argparse.git
        GIT_TAG 997da9255618311d1fcb0135ce86022729d1f1cb    # v2.9
        SOURCE_DIR ${EXTERNAL_INSTALL_LOCATION}/argparse
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
set(ARGPARSE_INCLUDE_DIR ${EXTERNAL_INSTALL_LOCATION}/argparse/include CACHE PATH "argparse include directory")

# Install targets for the external libraries
install(DIRECTORY ${EXTERNAL_INSTALL_LOCATION}/argparse/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/argparse)

FetchContent_MakeAvailable(fmt argparse)

# You can add more FetchContent_Declare and FetchContent_MakeAvailable commands for other libraries if needed

