# The core, gempba::gempba: libgempba, with multithreading built in. A shared library, so spdlog, fmt and hwloc stay
# inside it.
find_package(spdlog REQUIRED)
find_package(fmt REQUIRED)
message(STATUS "Found spdlog ${spdlog_VERSION} in ${spdlog_DIR}")
message(STATUS "Found fmt ${fmt_VERSION} in ${fmt_DIR}")
if (GEMPBA_HWLOC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
    message(STATUS "Found hwloc ${HWLOC_VERSION}")
endif ()
add_subdirectory(${PROJECT_SOURCE_DIR}/external ${PROJECT_BINARY_DIR}/external)

# What a component may use from the core. A component installed as source ships these headers with its own.
add_library(gempba_component_headers INTERFACE)
target_include_directories(gempba_component_headers INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/component>)
set_target_properties(gempba_component_headers PROPERTIES GEMPBA_COMPONENT_HEADERS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/component)

# The public headers use only the standard library (#412). spdlog and fmt are internal and header-only, so the libraries
# carry the code they use, hidden, and the exported targets don't name them. In-tree targets that include private headers
# link gempba_logging.
add_library(gempba_logging INTERFACE)
get_target_property(_spdlog_includes spdlog::spdlog_header_only INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(_spdlog_definitions spdlog::spdlog_header_only INTERFACE_COMPILE_DEFINITIONS)
target_include_directories(gempba_logging INTERFACE ${_spdlog_includes})
target_compile_definitions(gempba_logging INTERFACE SPDLOG_HEADER_ONLY ${_spdlog_definitions})
target_link_libraries(gempba_logging INTERFACE fmt::fmt-header-only)

configure_file(${PROJECT_SOURCE_DIR}/config.h.in ${PROJECT_BINARY_DIR}/gempba/config.h)

gempba_add_component(core INSTALL BINARY)

target_sources(gempba_objects
        PRIVATE FILE_SET cabi_headers TYPE HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES ${PROJECT_SOURCE_DIR}/include/gempba/cabi/gempba.h
)
target_include_directories(gempba_objects PRIVATE
        $<TARGET_PROPERTY:gempba_logging,INTERFACE_INCLUDE_DIRECTORIES>
        ${GEMPBA_BS_THREAD_POOL_INCLUDE_DIR}
)
target_compile_definitions(gempba_objects PRIVATE $<TARGET_PROPERTY:gempba_logging,INTERFACE_COMPILE_DEFINITIONS>)
target_link_libraries(gempba_objects PRIVATE fmt::fmt-header-only)

# The JNI library ships alone inside the JAR, so it links hwloc statically (#408).
if (GEMPBA_HWLOC AND GEMPBA_BUILD_JAVA_BINDING)
    find_library(GEMPBA_HWLOC_ARCHIVE NAMES libhwloc.a HINTS ${HWLOC_STATIC_LIBRARY_DIRS} NO_DEFAULT_PATH REQUIRED)
    set(_hwloc_static_dependencies ${HWLOC_STATIC_LIBRARIES})
    list(REMOVE_ITEM _hwloc_static_dependencies hwloc)
    target_include_directories(gempba_objects PRIVATE ${HWLOC_STATIC_INCLUDE_DIRS})
    target_link_libraries(gempba_objects PRIVATE ${GEMPBA_HWLOC_ARCHIVE} ${_hwloc_static_dependencies} ${HWLOC_STATIC_LDFLAGS_OTHER})
    # hwloc.pc leaves out the frameworks hwloc's macOS backend calls: hwloc passes them only to its own shared library.
    if (APPLE)
        target_link_libraries(gempba_objects PRIVATE "-framework CoreFoundation" "-framework IOKit")
    endif ()
elseif (GEMPBA_HWLOC)
    target_link_libraries(gempba_objects PRIVATE PkgConfig::HWLOC)
endif ()

if (WIN32)
    # process_probe needs psapi (process info); telemetry TCP server needs ws2_32 (Winsock).
    target_link_libraries(gempba_objects PRIVATE psapi ws2_32)
endif ()
