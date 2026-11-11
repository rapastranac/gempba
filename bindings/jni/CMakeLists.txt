cmake_minimum_required(VERSION 3.28)
project(gempba_jni CXX)

# ─── JDK ──────────────────────────────────────────────────────────────────────
find_package(JNI REQUIRED)

# ─── Shared library ───────────────────────────────────────────────────────────
add_library(gempba_jni SHARED
    src/gempba_jni.cpp
)

set_target_properties(gempba_jni PROPERTIES
    CXX_STANDARD          23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS        OFF

    # Drop the "lib" prefix on all platforms so NativeLoader always requests
    # the same name (System.mapLibraryName adds the prefix/suffix back).
    PREFIX ""
)

# ─── Includes ─────────────────────────────────────────────────────────────────
target_include_directories(gempba_jni PRIVATE
    ${JNI_INCLUDE_DIRS}             # jni.h, jni_md.h
    ${CMAKE_SOURCE_DIR}/include     # gempba public headers
)

# ─── Link ─────────────────────────────────────────────────────────────────────
# gempba's objects go into the library, the MPI component's too when it is built, so Java users only need the single
# .so / .dll artifact. The JVM already provides every JNI function, so the library links no JVM library.
target_link_libraries(gempba_jni PRIVATE gempba_objects)
if (TARGET gempba_mpi_objects)
    target_link_libraries(gempba_jni PRIVATE gempba_mpi_objects)
    target_compile_definitions(gempba_jni PRIVATE GEMPBA_JNI_MPI=1)
endif ()

# The C++ runtime goes in too, so the library needs only the OS, the JVM and, for mp-mpi, MPI.
if (MINGW)
    target_link_options(gempba_jni PRIVATE -static)
elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(gempba_jni PRIVATE -static-libstdc++ -static-libgcc LINKER:--exclude-libs,ALL LINKER:--as-needed)
endif ()

# ─── Output directory ─────────────────────────────────────────────────────────
# Place the binary where Maven's resource tree expects it so "mvn package"
# bundles it into the JAR automatically.
# Pass -DGEMPBA_JNI_CLASSIFIER=linux-x86_64 (or win-x86_64, osx-aarch64, …)
# at configure time.
set(GEMPBA_JNI_NATIVES_DIR ${CMAKE_SOURCE_DIR}/bindings/java/src/main/resources/natives/${GEMPBA_JNI_CLASSIFIER})
set_target_properties(gempba_jni PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${GEMPBA_JNI_NATIVES_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${GEMPBA_JNI_NATIVES_DIR}   # Windows DLL land here
)

# ─── The MPI it is built against ──────────────────────────────────────────────
# gempba_jni.properties, next to a library with the MPI component, names its MPI so NativeLoader can say which MPI a
# library that fails to load needs. The two variants share the directory, so a build without MPI removes it.
if (TARGET gempba_mpi_objects)
    set(MPI_CXX_SKIP_MPICXX ON)
    find_package(MPI REQUIRED)
    include(CheckCXXSymbolExists)
    set(CMAKE_REQUIRED_LIBRARIES MPI::MPI_CXX)
    set(CMAKE_REQUIRED_QUIET ON)
    check_cxx_symbol_exists(OPEN_MPI mpi.h GEMPBA_JNI_OPEN_MPI)
    check_cxx_symbol_exists(MSMPI_VER mpi.h GEMPBA_JNI_MS_MPI)
    check_cxx_symbol_exists(I_MPI_VERSION mpi.h GEMPBA_JNI_INTEL_MPI)
    check_cxx_symbol_exists(MPICH_VERSION mpi.h GEMPBA_JNI_MPICH)
    unset(CMAKE_REQUIRED_LIBRARIES)
    unset(CMAKE_REQUIRED_QUIET)

    if (GEMPBA_JNI_OPEN_MPI)
        set(gempba_jni_mpi "Open MPI")
    elseif (GEMPBA_JNI_MS_MPI)
        set(gempba_jni_mpi "MS-MPI")
    elseif (GEMPBA_JNI_INTEL_MPI)
        set(gempba_jni_mpi "Intel MPI")
    elseif (GEMPBA_JNI_MPICH)
        set(gempba_jni_mpi "MPICH")
    else ()
        set(gempba_jni_mpi "an unidentified MPI")
    endif ()
    message(STATUS "gempba_jni: built against ${gempba_jni_mpi}")

    file(CONFIGURE OUTPUT gempba_jni.properties CONTENT "mpi=${gempba_jni_mpi}\n")
    add_custom_command(TARGET gempba_jni POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_BINARY_DIR}/gempba_jni.properties ${GEMPBA_JNI_NATIVES_DIR}
    )
else ()
    add_custom_command(TARGET gempba_jni POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f ${GEMPBA_JNI_NATIVES_DIR}/gempba_jni.properties
    )
endif ()
