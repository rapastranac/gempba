#!/bin/bash
# Build the gempba Linux JAR for one variant.
#
# Usage:
#   bash scripts/build_jar_linux.sh MT   # multithreading — no MPI runtime needed
#   bash scripts/build_jar_linux.sh MP   # multiprocessing — links system MPI
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
# halfway through (the most common trap is a shell that has cmake but no
# Maven on PATH, which is exactly what happens when the only Maven on the
# host is IntelliJ-bundled).
for tool in cmake mvn; do
    command -v "$tool" >/dev/null || {
        echo "Required tool '$tool' is not on PATH." >&2
        echo "Install it (or prepend its bin/ dir to PATH) and rerun." >&2
        exit 2
    }
done

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)

arch=$(uname -m)
case "$arch" in
    x86_64|amd64)  classifier=linux-x86_64  ;;
    aarch64|arm64) classifier=linux-aarch64 ;;
    *) echo "Unsupported arch: $arch" >&2; exit 2 ;;
esac

build_dir="$WORKSPACE/.build-${variant}"
native_dir="$WORKSPACE/bindings/java/src/main/resources/natives/${classifier}"
native_lib="$native_dir/gempba_jni.so"

# jni/CMakeLists.txt sends gempba_jni.so straight into the shared resources
# tree, so an MT and MP build of this script overwrite the same file.  If
# both build dirs already exist and the file mtime sits between MT's and
# MP's prerequisites, GNU make sees "output newer than inputs" and skips
# the relink — the JAR ends up packaging the previous variant's native.
# Wipe the file before each cmake build to guarantee a fresh link.
rm -f "$native_lib"

cmake -B "$build_dir" -S "$WORKSPACE" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGEMPBA_BUILD_JAVA_BINDING=ON \
    -DGEMPBA_JNI_CLASSIFIER="$classifier" \
    -DGEMPBA_MULTIPROCESSING="$mp_flag" \
    -DGEMPBA_HWLOC=ON \
    -DGEMPBA_DEV_MODE=OFF \
    -DGEMPBA_BUILD_TESTS=OFF \
    -DGEMPBA_DEBUG_COMMENTS=OFF

cmake --build "$build_dir" --target gempba_jni -j"$(nproc)"

cd "$WORKSPACE/bindings/java"
# `install` (not `package`) so the produced JAR also lands in
# ~/.m2/repository/io/gempba/gempba/<version>/ — that's what a
# downstream <dependency> on this artifact resolves against.
mvn install -DskipTests -Dgempba.variant="$variant"
