cmake_minimum_required(VERSION 3.2)
project(run_tests)

set(CMAKE_CXX_STANDARD 20)

# Include the external libraries' headers
include_directories("${GTEST_INSTALL_LOCATION}/googletest/include")

# Create a binary directory for the GTest library
set(GTEST_BINARY_DIR "${CMAKE_BINARY_DIR}/gtest")
file(MAKE_DIRECTORY ${GTEST_BINARY_DIR})

# Add the GTest library with modified target names
add_library(my_gtest STATIC IMPORTED)
set_target_properties(my_gtest PROPERTIES
        IMPORTED_LOCATION "${GTEST_INSTALL_LOCATION}/gtest/libgtest.a"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INSTALL_LOCATION}/gtest/googletest/include"
        )

add_library(my_gtest_main STATIC IMPORTED)
set_target_properties(my_gtest_main PROPERTIES
        IMPORTED_LOCATION "${GTEST_INSTALL_LOCATION}/gtest/libgtest_main.a"
        INTERFACE_LINK_LIBRARIES "my_gtest"
        )

add_library(my_gmock STATIC IMPORTED)
set_target_properties(my_gmock PROPERTIES
        IMPORTED_LOCATION "${GTEST_INSTALL_LOCATION}/gtest/libgmock.a"
        INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INSTALL_LOCATION}/gtest/googlemock/include"
        INTERFACE_LINK_LIBRARIES "my_gtest"
        )

add_library(my_gmock_main STATIC IMPORTED)
set_target_properties(my_gmock_main PROPERTIES
        IMPORTED_LOCATION "${GTEST_INSTALL_LOCATION}/gtest/libgmock_main.a"
        INTERFACE_LINK_LIBRARIES "my_gmock;my_gtest"
        )

# Add the source files from the main src folder
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/*.cpp")

# Add the test source files
file(GLOB_RECURSE TEST_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

# Create an executable for the tests
add_executable(tests ${SOURCE_FILES} ${TEST_SOURCE_FILES})

# Link against the modified Google Test and other necessary libraries
target_link_libraries(tests my_gtest_main ${CMAKE_THREAD_LIBS_INIT})

# Add the tests to CTest
include(GoogleTest)
gtest_discover_tests(tests)
