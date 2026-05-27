#!/bin/bash
# Build the gempba macOS JAR for one variant.
#
# Usage:
#   bash scripts/build_jar_macos.sh MT   # multithreading — no MPI runtime needed
#   bash scripts/build_jar_macos.sh MP   # multiprocessing — links OpenMPI
#
# Each variant gets its own .build-${variant}/ directory so the two CMake
# caches don't collide.

set -e

usage() {
    echo "Usage: $(basename "$0") MT|MP" >&2
    exit 2
}

case "${1:-}" in
    MT) variant=multithreading;  mp_flag=OFF ;;
    MP) variant=multiprocessing; mp_flag=ON  ;;
    *)  usage ;;
esac

# Fail fast on missing tools instead of letting `set -e` exit silently
# halfway through.  brew is needed up-front to resolve libomp / boost
# prefixes for the cmake flags below; cmake + mvn are the actual build
# drivers.
for tool in brew cmake mvn; do
    command -v "$tool" >/dev/null || {
        echo "Required tool '$tool' is not on PATH." >&2
        echo "Install it (or prepend its bin/ dir to PATH) and rerun." >&2
        exit 2
    }
done

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)

arch=$(uname -m)
case "$arch" in
    x86_64) classifier=osx-x86_64  ;;
    arm64)  classifier=osx-aarch64 ;;
    *) echo "Unsupported arch: $arch" >&2; exit 2 ;;
esac

build_dir="$WORKSPACE/.build-${variant}"
native_dir="$WORKSPACE/bindings/java/src/main/resources/natives/${classifier}"
native_lib="$native_dir/gempba_jni.dylib"

# jni/CMakeLists.txt sends gempba_jni.dylib straight into the shared
# resources tree, so an MT and MP build of this script overwrite the same
# file.  See build_jar_linux.sh for the full rationale; same risk on
# macOS (Make would skip the relink and the JAR would bundle the wrong
# variant's native).  Wipe the file before each cmake build.
rm -f "$native_lib"

# Apple clang doesn't ship with OpenMP support; libomp comes from
# Homebrew.  Mirrors the OpenMP override block in c-cpp-macos.yml so
# the JNI shared library finds the same omp runtime gempba's own tests
# build against.
cmake -B "$build_dir" -S "$WORKSPACE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGEMPBA_BUILD_JAVA_BINDING=ON \
    -DGEMPBA_JNI_CLASSIFIER="$classifier" \
    -DGEMPBA_MULTIPROCESSING="$mp_flag" \
    -DGEMPBA_HWLOC=ON \
    -DGEMPBA_DEV_MODE=OFF \
    -DGEMPBA_BUILD_TESTS=OFF \
    -DGEMPBA_DEBUG_COMMENTS=OFF \
    -DOpenMP_CXX_FLAGS="-Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include" \
    -DOpenMP_CXX_LIB_NAMES="omp" \
    -DOpenMP_omp_LIBRARY="$(brew --prefix libomp)/lib/libomp.dylib" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix boost)"

cmake --build "$build_dir" --target gempba_jni -j"$(sysctl -n hw.logicalcpu)"

cd "$WORKSPACE/bindings/java"
# `install` (not `package`) so the produced JAR also lands in
# ~/.m2/repository/io/gempba/gempba/<version>/ — that's what a
# downstream <dependency> on this artifact resolves against.
mvn install -DskipTests -Dgempba.variant="$variant"
