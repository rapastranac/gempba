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
    ${CMAKE_SOURCE_DIR}/private     # gempba private headers
)

# ─── Link ─────────────────────────────────────────────────────────────────────
# gempba is a STATIC library; linking it into a SHARED lib embeds all its
# symbols, so Java users only need the single .so / .dll artifact.
# MPI, fmt and Threads are transitively pulled in from gempba's
# PUBLIC link interface.
target_link_libraries(gempba_jni PRIVATE
    gempba
    ${JNI_LIBRARIES}
)

# ─── Output directory ─────────────────────────────────────────────────────────
# Place the binary where Maven's resource tree expects it so "mvn package"
# bundles it into the JAR automatically.
# Pass -DGEMPBA_JNI_CLASSIFIER=linux-x86_64 (or win-x86_64, osx-aarch64, …)
# at configure time.
set_target_properties(gempba_jni PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY
        ${CMAKE_SOURCE_DIR}/bindings/java/src/main/resources/natives/${GEMPBA_JNI_CLASSIFIER}
    RUNTIME_OUTPUT_DIRECTORY   # Windows DLL land here
        ${CMAKE_SOURCE_DIR}/bindings/java/src/main/resources/natives/${GEMPBA_JNI_CLASSIFIER}
)
