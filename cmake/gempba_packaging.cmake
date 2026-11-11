# ─── Common metadata ─────────────────────────────────────────────────────────
set(CPACK_PACKAGE_VENDOR "Andres Pastrana")
set(CPACK_PACKAGE_CONTACT "gempba@outlook.com")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rapastranac/gempba")

if (EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif ()

# ─── One package per component that gempba_add_component() declared ─────────
set(CPACK_DEB_COMPONENT_INSTALL ON)
get_property(CPACK_COMPONENTS_ALL GLOBAL PROPERTY GEMPBA_COMPONENTS)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_DEBIAN_CORE_PACKAGE_NAME "libgempba-dev")
set(CPACK_COMPONENT_CORE_DESCRIPTION
    "GemPBA, the core (development files)
GemPBA's core: libgempba.so with multithreading built in, the header tree, gempbaConfig.cmake and gempba.pc. Its own dependencies are inside the library, so a consumer needs only the C++ standard library. Install libgempba-mpi-dev to add the MPI component, find_package(gempba COMPONENTS mpi).")
set(CPACK_DEBIAN_CORE_PACKAGE_DEPENDS "libhwloc15, libc6 (>= 2.38), libstdc++6 (>= 13.1)")

set(CPACK_DEBIAN_MPI_PACKAGE_NAME "libgempba-mpi-dev")
set(CPACK_COMPONENT_MPI_DESCRIPTION
    "GemPBA, the MPI component (development files)
GemPBA's MPI component as source: its headers, sources and CMake code, on top of libgempba-dev. find_package(gempba COMPONENTS mpi) compiles it in the consumer's build against the MPI installed there, Open MPI or MPICH.")
set(CPACK_DEBIAN_MPI_PACKAGE_DEPENDS "libgempba-dev (= ${CMAKE_PROJECT_VERSION}), libopenmpi-dev (>= 4.0) | libmpich-dev")

# ─── DEB-specific ─────────────────────────────────────────────────────────────
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/rapastranac/gempba")

include(CPack)
