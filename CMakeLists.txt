cmake_minimum_required(VERSION 3.22.1)

project(gempba)

set(CMAKE_CXX_STANDARD 23)

find_package(OpenMP REQUIRED)
find_package(Threads REQUIRED)
find_package(MPI REQUIRED)
find_package(Boost COMPONENTS system serialization fiber REQUIRED)
#find_package(Boost REQUIRED)
find_package(GTest REQUIRED)    # Find Google Test
include_directories(${GTEST_INCLUDE_DIRS})

include_directories(${MPI_INCLUDE_PATH})


link_directories(${Boost_LIBRARY_DIRS})
include_directories(${Boost_INCLUDE_DIRS})
Set(CMAKE_EXE_LINKER_FLAGS "-lboost_system -lboost_serialization  -lboost_fiber")

# Other project-specific configurations
add_subdirectory(external)


add_definitions(-DMPI_ENABLED)
#add_definitions(-DDEBUG_COMMENTS)
#add_definitions(-DVC_VOID)
#add_definitions(-DVC_VOID_MPI)
add_definitions(-DBITVECTOR_VC)
#add_definitions(-DBITVECTOR_VC_THREAD)
#add_definitions(-DVC_NON_VOID)
#add_definitions(-DVC_NON_VOID_MPI)

#add_definitions(-DSCHEDULER_CENTRALIZED)
#add_definitions(-DUSE_LARGE_ENCODING)

add_compile_options("-std=c++23" "-fconcepts" "-O3" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread")


#valgrind
#add_compile_options("-g" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts" "-ggdb3")

# Set CMake policy
cmake_policy(SET CMP0110 NEW)
# Enable testing
enable_testing()

#sets the executable in main folder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

include_directories("include")
include_directories("GemPBA")
add_subdirectory("tests")
include_directories(${FMT_INCLUDE_DIR})
include_directories(${ARGPARSE_INCLUDE_DIR})


message("fmt dir: ${FMT_INCLUDE_DIR}")
message("Parser dir: ${ARGPARSE_INCLUDE_DIR}")

file(GLOB Sources "src/*.cpp")

add_executable(a.out ${Sources}
        ${FMT_INSTALL_LOCATION}/fmt/src/format.cc
        ${FMT_INSTALL_LOCATION}/fmt/src/os.cc)

target_link_libraries(a.out PRIVATE
        ${CMAKE_THREAD_LIBS_INIT}
        ${MPI_LIBRARIES}
        ${Boost_LIBRARIES}
        OpenMP::OpenMP_CXX
        ${GTEST_LIBRARIES}
        ${GTEST_MAIN_LIBRARIES}
        )

if (MPI_COMPILE_FLAGS)
    set_target_properties(a.out PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif ()