cmake_minimum_required(VERSION 3.28)
include(FetchContent)

project(external)

## CPM
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")

if (NOT (EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake (latest release)")
    file(DOWNLOAD
            https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/CPM.cmake
            ${CPM_DOWNLOAD_LOCATION}
            STATUS _cpm_download_status
            TLS_VERIFY ON)
    list(GET _cpm_download_status 0 _cpm_download_code)
    list(GET _cpm_download_status 1 _cpm_download_msg)
    if (NOT _cpm_download_code EQUAL 0)
        file(REMOVE ${CPM_DOWNLOAD_LOCATION})
        message(FATAL_ERROR "Failed to download CPM.cmake (status ${_cpm_download_code}): ${_cpm_download_msg}")
    endif ()
endif ()

include(${CPM_DOWNLOAD_LOCATION})

if (NOT COMMAND CPMAddPackage)
    file(REMOVE ${CPM_DOWNLOAD_LOCATION})
    message(FATAL_ERROR "CPM.cmake included but CPMAddPackage is undefined; "
            "the cached download at ${CPM_DOWNLOAD_LOCATION} was likely corrupted "
            "and has been removed. Re-run the build.")
endif ()

CPMAddPackage(
        NAME BS_thread_pool
        GITHUB_REPOSITORY rapastranac/thread-pool
        GIT_TAG master)
add_library(BS_thread_pool INTERFACE)
target_include_directories(BS_thread_pool INTERFACE ${BS_thread_pool_SOURCE_DIR}/include)

# Propagate fetched source dir to parent scope so root CMakeLists.txt can use it
set(GEMPBA_BS_THREAD_POOL_INCLUDE_DIR "${BS_thread_pool_SOURCE_DIR}/include" CACHE PATH "" FORCE)

# Define 'external_libs' as an INTERFACE library
add_library(external_libs INTERFACE)

# Link argparse to 'external_libs'
target_link_libraries(external_libs
        INTERFACE
        BS_thread_pool)