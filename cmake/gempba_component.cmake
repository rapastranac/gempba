# gempba_add_component(): what every component of gempba needs, in one place (#436).
include(CheckCXXSourceCompiles)

find_package(Threads REQUIRED)

# std::stacktrace lives in libstdc++exp, which some GCC builds, such as Red Hat's gcc-toolset, don't ship.
set(CMAKE_REQUIRED_LIBRARIES stdc++exp)
check_cxx_source_compiles([=[
    #include <stacktrace>
    int main() { return static_cast<int>(std::stacktrace::current().size()); }
]=] GEMPBA_STACKTRACE_LINKS)
unset(CMAKE_REQUIRED_LIBRARIES)
if (GEMPBA_STACKTRACE_LINKS)
    set(GEMPBA_PC_STACKTRACE_LIBS "-lstdc++exp")
    set(GEMPBA_PC_STACKTRACE_CFLAGS "")
else ()
    set(GEMPBA_PC_STACKTRACE_LIBS "")
    set(GEMPBA_PC_STACKTRACE_CFLAGS "-DGEMPBA_HAS_STACKTRACE=0")
endif ()

# On Linux and macOS a shared library also exports the std:: template code it instantiates with spdlog's and fmt's types,
# since the standard library gives its templates default visibility. The export list keeps only gempba's own symbols.
if (APPLE)
    set(GEMPBA_EXPORT_LIST "${PROJECT_SOURCE_DIR}/cmake/gempba.exports")
    set(GEMPBA_EXPORT_LIST_OPTION "LINKER:-exported_symbols_list,${GEMPBA_EXPORT_LIST}")
elseif (UNIX)
    set(GEMPBA_EXPORT_LIST "${PROJECT_SOURCE_DIR}/cmake/gempba.map")
    set(GEMPBA_EXPORT_LIST_OPTION "LINKER:--version-script=${GEMPBA_EXPORT_LIST}")
endif ()

# Where a component installed as source puts its sources and private headers: <this>/<component>/{src,private}.
set(GEMPBA_INSTALL_COMPONENT_SOURCEDIR ${CMAKE_INSTALL_DATADIR}/gempba)

# What every target built from gempba's code needs, consumers included: the public headers, the threads library and
# std::stacktrace's library, which node.hpp's inline code uses.
function(gempba_add_public_usage p_target)
    target_include_directories(${p_target} PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${p_target} PUBLIC Threads::Threads)
    if (GEMPBA_STACKTRACE_LINKS)
        target_link_libraries(${p_target} PUBLIC stdc++exp)
    else ()
        target_compile_definitions(${p_target} PUBLIC GEMPBA_HAS_STACKTRACE=0)
    endif ()
    if (UNIX AND NOT APPLE)
        target_link_options(${p_target} PUBLIC -rdynamic)
    endif ()
endfunction()

# How gempba's own sources compile. Only what export.h marks leaves a shared library.
function(gempba_configure_objects p_target p_building_define)
    set_target_properties(${p_target} PROPERTIES
            POSITION_INDEPENDENT_CODE ON
            CXX_VISIBILITY_PRESET hidden
    )
    target_compile_definitions(${p_target} PRIVATE
            ${p_building_define}
            $<$<BOOL:${GEMPBA_HWLOC}>:GEMPBA_HWLOC>
            $<$<BOOL:${GEMPBA_DEBUG_COMMENTS}>:GEMPBA_DEBUG_COMMENTS>
            $<$<BOOL:${GEMPBA_BUILD_TESTS}>:GEMPBA_BUILD_TESTS>
    )
    target_compile_options(${p_target} PRIVATE
            -pthread -fexceptions
            $<$<CONFIG:Debug>:-Wall -Wextra -Wpedantic -g -O0 -ggdb3 -DGEMPBA_DEBUG_COMMENTS>
            $<$<CONFIG:Release>:-O3 -DNDEBUG>
    )
    gempba_add_public_usage(${p_target})
endfunction()

# gempba_add_component(<name>
#     [DEPENDS <target>...]          what its library links publicly; gempba::gempba for every component but the core
#     [PUBLIC_HEADERS <path>...]     its headers under include/, which the core's install leaves out
#     [COMPILE <bool>]               whether this build compiles it (default ON)
#     INSTALL BINARY|SOURCE)         the library and its targets file, or its sources for the consumer's build
#
# Called from the component's directory: every .cpp under it is a source, and its private/ holds its private headers.
# Makes <target>_objects, the objects the tests and the JNI library link; <target>, the shared library; and the
# gempba::<name> alias. <target> is gempba for the core and gempba_<name> for the others.
function(gempba_add_component p_name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "INSTALL;COMPILE" "DEPENDS;PUBLIC_HEADERS")
    if (NOT arg_INSTALL MATCHES "^(BINARY|SOURCE)$")
        message(FATAL_ERROR "gempba_add_component(${p_name}): INSTALL must be BINARY or SOURCE")
    endif ()
    if (NOT DEFINED arg_COMPILE)
        set(arg_COMPILE ON)
    endif ()
    if (p_name STREQUAL "core")
        set(v_target gempba)
        set(v_export gempba)
    else ()
        set(v_target gempba_${p_name})
        set(v_export ${p_name})
    endif ()
    string(TOUPPER "${p_name}" v_upper)
    set(v_private "${CMAKE_CURRENT_SOURCE_DIR}/private")

    set_property(GLOBAL APPEND PROPERTY GEMPBA_COMPONENTS ${p_name})
    set_property(GLOBAL PROPERTY GEMPBA_PUBLIC_HEADERS_${p_name} ${arg_PUBLIC_HEADERS})

    if (arg_COMPILE)
        file(GLOB_RECURSE v_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
        add_library(${v_target}_objects OBJECT ${v_sources})
        gempba_configure_objects(${v_target}_objects GEMPBA_BUILDING_${v_upper})
        target_link_libraries(${v_target}_objects PUBLIC gempba_component_headers)
        if (IS_DIRECTORY "${v_private}")
            target_include_directories(${v_target}_objects PUBLIC $<BUILD_INTERFACE:${v_private}>)
        endif ()

        add_library(${v_target} SHARED)
        target_link_libraries(${v_target} PRIVATE ${v_target}_objects)
        gempba_add_public_usage(${v_target})
        # The objects get the core's component headers instead of the core's library, so the tests and the JNI
        # library, which link the objects, don't link the core twice.
        foreach (v_dependency IN LISTS arg_DEPENDS)
            target_link_libraries(${v_target} PUBLIC ${v_dependency})
            if (NOT v_dependency MATCHES "^gempba::")
                target_link_libraries(${v_target}_objects PUBLIC ${v_dependency})
            endif ()
        endforeach ()
        set_target_properties(${v_target} PROPERTIES
                EXPORT_NAME ${v_export}
                VERSION ${PROJECT_VERSION}
                SOVERSION ${PROJECT_VERSION_MAJOR}
                LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
                RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
                ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}
        )
        add_library(gempba::${v_export} ALIAS ${v_target})
        if (GEMPBA_EXPORT_LIST)
            target_link_options(${v_target} PRIVATE ${GEMPBA_EXPORT_LIST_OPTION})
            set_property(TARGET ${v_target} APPEND PROPERTY LINK_DEPENDS ${GEMPBA_EXPORT_LIST})
        endif ()
    endif ()

    if (arg_INSTALL STREQUAL "BINARY")
        if (NOT arg_COMPILE)
            message(FATAL_ERROR "gempba_add_component(${p_name}): INSTALL BINARY needs COMPILE ON")
        endif ()
        if (p_name STREQUAL "core")
            set(v_targets_file gempba-targets.cmake)
        else ()
            set(v_targets_file gempba-${p_name}-targets.cmake)
        endif ()
        install(TARGETS ${v_target}
                EXPORT gempba_${p_name}_targets
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${p_name}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT ${p_name}
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${p_name}
                INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
        install(EXPORT gempba_${p_name}_targets
                FILE ${v_targets_file}
                NAMESPACE gempba::
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
                COMPONENT ${p_name}
        )
    else ()
        set(v_destination ${GEMPBA_INSTALL_COMPONENT_SOURCEDIR}/${p_name})
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
                DESTINATION ${v_destination}/src
                COMPONENT ${p_name}
                FILES_MATCHING PATTERN "*.cpp"
                PATTERN "private" EXCLUDE
        )
        if (IS_DIRECTORY "${v_private}")
            install(DIRECTORY ${v_private}/ DESTINATION ${v_destination}/private COMPONENT ${p_name})
        endif ()
        get_target_property(v_component_headers gempba_component_headers GEMPBA_COMPONENT_HEADERS_DIR)
        install(DIRECTORY ${v_component_headers}/ DESTINATION ${v_destination}/private COMPONENT ${p_name})
    endif ()

    if (NOT p_name STREQUAL "core")
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/gempba-${p_name}.cmake
                DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/gempba
                COMPONENT ${p_name}
        )
    endif ()
endfunction()
