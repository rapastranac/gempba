# Options and build-wide settings. Defaults depend on whether gempba is the root project or a subproject; option()
# only writes the cache when the key is missing, so a -D on the command line always wins.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug builds export their symbols on Linux so backtraces name them.
if (NOT MSVC AND UNIX)
    add_link_options($<$<CONFIG:Debug>:-rdynamic>)
endif ()

if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    message(STATUS "GemPBA detected as root project. Defaulting to build tests.")
    set(_gempba_default ON)

    if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Debug CACHE STRING "Default build type for GemPBA")
        message(STATUS "No build type specified. Defaulting to Debug.")
    else ()
        message(STATUS "Using CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif ()
else ()
    message(STATUS "GemPBA detected as a subproject. Defaulting to not build tests.")
    set(_gempba_default OFF)
    message(STATUS "Using parent project's CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
endif ()

option(GEMPBA_BUILD_TESTS        "Build gempba unit tests"                                                         ${_gempba_default})
option(GEMPBA_BUILD_MPI          "Compile the MPI component, gempba::mpi, in this build; installs ship its source" ${_gempba_default})
option(GEMPBA_HWLOC              "Use hwloc for hardware topology"                                                 ${_gempba_default})
option(GEMPBA_DEBUG_COMMENTS     "Enable debug comments in gempba"                                                 OFF)
option(GEMPBA_BUILD_JAVA_BINDING "Build the Java JNI shared library (requires JDK)"                                OFF)
unset(_gempba_default)

if (GEMPBA_BUILD_JAVA_BINDING AND (NOT DEFINED GEMPBA_JNI_CLASSIFIER OR GEMPBA_JNI_CLASSIFIER STREQUAL ""))
    message(FATAL_ERROR
            "GEMPBA_BUILD_JAVA_BINDING requires -DGEMPBA_JNI_CLASSIFIER=<os>-<arch> "
            "(e.g. linux-x86_64, win-x86_64, osx-aarch64)")
endif ()

message(STATUS "🔧 GEMPBA_BUILD_TESTS=${GEMPBA_BUILD_TESTS}")
message(STATUS "🔧 GEMPBA_BUILD_MPI=${GEMPBA_BUILD_MPI}")
message(STATUS "🔧 GEMPBA_HWLOC=${GEMPBA_HWLOC}")
message(STATUS "🔧 GEMPBA_DEBUG_COMMENTS=${GEMPBA_DEBUG_COMMENTS}")
