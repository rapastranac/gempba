cmake_minimum_required(VERSION 3.2)
include(FetchContent)

project(external)

set(EXTERNAL_INSTALL_LOCATION ${CMAKE_SOURCE_DIR}/external CACHE PATH "External install location" FORCE)

#region FMT
# Set the installation location for fmt
set(FMT_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "Fmt install location" FORCE)

# Define a custom variable for the include directory
set(FMT_INCLUDE_DIR ${FMT_INSTALL_LOCATION}/fmt/include CACHE PATH "fmt include directory")

# Install targets for the external libraries
install(DIRECTORY ${FMT_INSTALL_LOCATION}/fmt/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/fmt)

#endregion

#region ARGPARSE

# Set the installation location for fmt
set(ARGPARSE_INSTALL_LOCATION ${EXTERNAL_INSTALL_LOCATION} CACHE PATH "argparse install location" FORCE)

# Define a custom variable for the include directory
set(ARGPARSE_INCLUDE_DIR ${ARGPARSE_INSTALL_LOCATION}/argparse/include CACHE PATH "argparse include directory")

# Install targets for the external libraries
install(DIRECTORY ${ARGPARSE_INSTALL_LOCATION}/argparse/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/external/argparse)

#endregion

