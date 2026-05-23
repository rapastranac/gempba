cmake_minimum_required(VERSION 3.28)

project(gempba_examples LANGUAGES CXX)

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# 📦 Subprojects
add_subdirectory(external)

# Static linking of Boost
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost COMPONENTS serialization fiber REQUIRED)

# Include Boost y argparse
include_directories(${Boost_INCLUDE_DIRS})
include_directories(${ARGPARSE_INCLUDE_DIR})

# Each mode has its own subtree (multithreading/{src,include}, multiprocessing/
# {src,include}) and its own CMakeLists that drives its example targets. The
# multiprocessing/ subtree consumes gempba::multiprocessing::* and is only
# added when GEMPBA_MULTIPROCESSING=ON. The mp_*.cpp files still at the root
# move into the multiprocessing/ subtree in a later commit.

add_subdirectory(multithreading)
if (GEMPBA_MULTIPROCESSING)
    add_subdirectory(multiprocessing)
endif ()