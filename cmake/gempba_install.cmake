# What gempba installs beyond each component's own files: the public headers, the find_package and pkg-config files, and
# the packages. Included last, once every component is declared.

# ─── Public headers: a component's PUBLIC_HEADERS with it, the rest with the core ──
get_property(_gempba_components GLOBAL PROPERTY GEMPBA_COMPONENTS)
set(_gempba_header_excludes)
foreach (_gempba_component IN LISTS _gempba_components)
    get_property(_gempba_headers GLOBAL PROPERTY GEMPBA_PUBLIC_HEADERS_${_gempba_component})
    foreach (_gempba_header IN LISTS _gempba_headers)
        string(REPLACE "." "\\." _gempba_header_regex "${_gempba_header}")
        list(APPEND _gempba_header_excludes REGEX "/include/${_gempba_header_regex}(/.*)?$" EXCLUDE)
        cmake_path(GET _gempba_header PARENT_PATH _gempba_header_dir)
        if (IS_DIRECTORY "${PROJECT_SOURCE_DIR}/include/${_gempba_header}")
            install(DIRECTORY include/${_gempba_header}
                    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${_gempba_header_dir}
                    COMPONENT ${_gempba_component}
            )
        else ()
            install(FILES include/${_gempba_header}
                    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${_gempba_header_dir}
                    COMPONENT ${_gempba_component}
            )
        endif ()
    endforeach ()
endforeach ()

install(DIRECTORY include/gempba
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        COMPONENT core
        FILES_MATCHING
            PATTERN "*.hpp"
            PATTERN "*.h"
            ${_gempba_header_excludes}
)
install(FILES ${PROJECT_BINARY_DIR}/gempba/config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/gempba
        COMPONENT core
)

# ─── find_package(gempba) and pkg-config ─────────────────────────────────────
configure_package_config_file(
        cmake/gempbaConfig.cmake.in
        ${PROJECT_BINARY_DIR}/gempbaConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
        PATH_VARS GEMPBA_INSTALL_COMPONENT_SOURCEDIR
)
write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/gempbaConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)
install(FILES
        ${PROJECT_BINARY_DIR}/gempbaConfig.cmake
        ${PROJECT_BINARY_DIR}/gempbaConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
        COMPONENT core
)

configure_file(cmake/gempba.pc.in ${PROJECT_BINARY_DIR}/gempba.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/gempba.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        COMPONENT core
)

# ─── Packages ────────────────────────────────────────────────────────────────
include(cmake/gempba_packaging.cmake)
