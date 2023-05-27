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


FetchContent_MakeAvailable(fmt)

# You can add more FetchContent_Declare and FetchContent_MakeAvailable commands for other libraries if needed

