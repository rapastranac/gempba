cmake_minimum_required(VERSION 3.28)
include(FetchContent)

project(external)

## CPM
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
if (NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake")
    file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif ()
include(${CPM_DOWNLOAD_LOCATION})

CPMAddPackage("gh:p-ranav/argparse@3.0")

# Define 'external_libs' as an INTERFACE library
add_library(external_libs INTERFACE)

# Link argparse to 'external_libs'
target_link_libraries(external_libs INTERFACE argparse)