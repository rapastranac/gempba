# ─── Common metadata ─────────────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "libgempba-dev")
set(CPACK_PACKAGE_VENDOR "Andres Pastrana")
set(CPACK_PACKAGE_CONTACT "gempba@outlook.com")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "GemPBA: parallel branch-and-bound framework (development files)")
set(CPACK_PACKAGE_DESCRIPTION
    "GemPBA is a C++23 header and static library for generic massive \
parallelisation of branching algorithms using multithreading and/or \
MPI multiprocessing with quasi-horizontal load balancing.")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rapastranac/gempba")

if (EXISTS "${CMAKE_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
endif ()

# ─── DEB-specific ─────────────────────────────────────────────────────────────
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libopenmpi-dev (>= 4.0), libspdlog-dev, libfmt-dev, libgomp1, libc6 (>= 2.17)")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/rapastranac/gempba")

include(CPack)
