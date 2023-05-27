cmake_minimum_required(VERSION 3.2)

#------------------------------------------------------------------------------
# Build the google test library

# We compile Google Test ourselves instead of using pre-compiled libraries.
# See the Google Test FAQ "Why is it not recommended to install a
# pre-compiled copy of Google Test (for example, into /usr/local)?"
# at http://code.google.com/p/googletest/wiki/FAQ for more details.
add_library(gmock STATIC
        gmock-gtest-all.cc gmock/gmock.h gtest/gtest.h gtest/gtest-spi.h)
target_compile_definitions(gmock PUBLIC GTEST_HAS_STD_WSTRING=1)
target_include_directories(gmock SYSTEM PUBLIC . gmock gtest)