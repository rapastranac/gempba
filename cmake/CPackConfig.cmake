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

# ─── Flavor-specific name + depends ──────────────────────────────────────────
if (GEMPBA_MULTIPROCESSING)
    set(CPACK_PACKAGE_NAME "libgempba-mpi-dev")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "GemPBA — MPI multiprocessing flavor (development files)")
    set(CPACK_PACKAGE_DESCRIPTION
        "GemPBA's MPI multiprocessing flavor: ships libgempba_mpi.a and the \
matching CMake targets file. Layered on top of libgempba-dev, which provides \
the headers and the gempbaConfig.cmake dispatcher; pick the mpi flavor at \
consumer side with find_package(gempba COMPONENTS mpi).")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libgempba-dev (= ${CMAKE_PROJECT_VERSION}), libopenmpi-dev (>= 4.0)")
else ()
    set(CPACK_PACKAGE_NAME "libgempba-dev")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "GemPBA — multithreading flavor (development files)")
    set(CPACK_PACKAGE_DESCRIPTION
        "GemPBA's multithreading flavor and base development files: header \
tree, gempbaConfig.cmake dispatcher, libgempba.a. The default flavor for \
fast local iteration; install libgempba-mpi-dev to add MPI support.")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libspdlog-dev, libfmt-dev, libhwloc-dev, libgomp1, libc6 (>= 2.17)")
endif ()

# ─── DEB-specific ─────────────────────────────────────────────────────────────
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/rapastranac/gempba")

include(CPack)
