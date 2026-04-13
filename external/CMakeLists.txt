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

CPMAddPackage(
        NAME BS_thread_pool
        GITHUB_REPOSITORY rapastranac/thread-pool
        GIT_TAG master)
add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)

# Define 'external_libs' as an INTERFACE library
add_library(external_libs INTERFACE)

# Link argparse to 'external_libs'
target_link_libraries(external_libs
        INTERFACE
        BS_thread_pool)