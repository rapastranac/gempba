cmake_minimum_required(VERSION 3.16)

project(global)

set(CMAKE_CXX_STANDARD 20)

find_package(OpenMP REQUIRED)
find_package(Threads  REQUIRED)
find_package(MPI  REQUIRED)
find_package(Boost  COMPONENTS system serialization fiber REQUIRED)
#find_package(Boost REQUIRED)

include_directories(${MPI_INCLUDE_PATH})


link_directories(${Boost_LIBRARY_DIRS})
include_directories(${Boost_INCLUDE_DIRS})
Set(CMAKE_EXE_LINKER_FLAGS  "-lboost_system -lboost_serialization  -lboost_fiber")


add_definitions(-DR_SEARCH=true)
add_definitions(-DMPI_ENABLED)
#add_definitions(-DDEBUG_COMMENTS)
#add_definitions(-DVC_VOID)
#add_definitions(-DVC_VOID_MPI)
add_definitions(-DBITVECTOR_VC)
#add_definitions(-DBITVECTOR_VC_THREAD)
#add_definitions(-DVC_NON_VOID)
#add_definitions(-DVC_NON_VOID_MPI)

add_definitions(-DSCHEDULER_CENTRALIZED)
add_definitions(-DUSE_LARGE_ENCODING)

add_compile_options("-std=c++2a" "-fconcepts" "-O3" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread")
#add_compile_options("-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts" )



#valgrind
add_compile_options("-g" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts" "-ggdb3")

#Here below for boost
#add_compile_options("-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts" )
#add_compile_options("-g" "-Wall" "-Wpedantic" "-Wextra" "-fexceptions" "-pthread" "-fconcepts" )

#sets the executable in main folder
set( CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} )

include_directories("include")
include_directories("GemPBA")
include_directories("argsparse" )
include_directories("fmt/include" )

file(GLOB Sources "src/*.cpp")

add_executable(a.out  ${Sources} 
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/format.cc
                    ${CMAKE_CURRENT_SOURCE_DIR}/fmt/src/os.cc)

target_link_libraries(a.out ${CMAKE_THREAD_LIBS_INIT}
                            ${MPI_LIBRARIES}
                            ${Boost_LIBRARIES}
                            OpenMP::OpenMP_CXX
                            )

if(MPI_COMPILE_FLAGS)
  set_target_properties(a.out PROPERTIES COMPILE_FLAGS "${MPI_COMPILE_FLAGS}")
endif()

