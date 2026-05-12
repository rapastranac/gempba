cmake_minimum_required(VERSION 3.28)

project(gempba_examples LANGUAGES CXX)

if (POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif ()

# Static linking of Boost
set(Boost_USE_STATIC_LIBS ON)

find_package(Boost COMPONENTS serialization fiber REQUIRED)

add_subdirectory(external)

add_subdirectory(multithreading)

if (GEMPBA_MULTIPROCESSING)
    add_subdirectory(multiprocessing)
endif ()
