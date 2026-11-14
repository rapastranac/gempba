#!/bin/bash
# Build the gempba Linux JAR for one variant against this machine's compiler and MPI, without Docker: for clusters
# and other hosts whose MPI the published gempba-mpi isn't built for. build_jar_linux.sh builds the portable JAR.
#
# Usage:
#   bash scripts/build_jar_host_linux.sh MT   # multithreading
#   bash scripts/build_jar_host_linux.sh MP   # multiprocessing, against the MPI whose mpicc/mpicxx is on PATH
#
# Needs a C++23 compiler, CMake, curl, Maven, a JDK (JAVA_HOME) and, for MP, MPI. Installs the JAR into ~/.m2 as
# io.github.rapastranac:gempba or gempba-mpi.

set -euo pipefail

case "${1:-}" in
    MT) variant=multithreading;  mp_flag=OFF; flavor=mt     ;;
    MP) variant=multiprocessing; mp_flag=ON;  flavor=mp-mpi ;;
    *)  echo "Usage: $(basename "$0") MT|MP" >&2; exit 2 ;;
esac

for tool in cmake curl mvn java; do
    command -v "$tool" >/dev/null || { echo "Required tool '$tool' is not on PATH." >&2; exit 2; }
done

case "$(uname -m)" in
    x86_64)  classifier=linux-x86_64  ;;
    aarch64) classifier=linux-aarch64 ;;
    *) echo "Unsupported architecture: $(uname -m)" >&2; exit 2 ;;
esac

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
build_dir="$WORKSPACE/.build-jni-host-${variant}"
deps_dir="$WORKSPACE/.build-jni-deps"
native_dir="$WORKSPACE/bindings/java/src/main/resources/natives/${classifier}"

rm -f "$native_dir/gempba_jni.so" "$build_dir/CMakeCache.txt"

bash "$WORKSPACE/bindings/jni/build_deps.sh" "$deps_dir"

PKG_CONFIG_PATH="$deps_dir/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" cmake -S "$WORKSPACE" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$deps_dir" \
    -DGEMPBA_BUILD_JAVA_BINDING=ON \
    -DGEMPBA_JNI_CLASSIFIER="$classifier" \
    -DGEMPBA_BUILD_MPI="$mp_flag" \
    -DGEMPBA_HWLOC=ON \
    -DGEMPBA_BUILD_TESTS=OFF \
    -DGEMPBA_DEBUG_COMMENTS=OFF
cmake --build "$build_dir" --target gempba_jni -j"$(nproc)"

if [[ -f "$native_dir/gempba_jni.properties" ]]; then
    echo "gempba_jni: $(cat "$native_dir/gempba_jni.properties")"
fi

# shellcheck source=_java_artifact.sh
source "$WORKSPACE/scripts/_java_artifact.sh"
gempba_java_install "$WORKSPACE" "$variant" "$flavor"
