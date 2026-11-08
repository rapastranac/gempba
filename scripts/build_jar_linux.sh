#!/bin/bash
# Build the gempba Linux JAR for one variant.
#
# Usage:
#   bash scripts/build_jar_linux.sh MT   # multithreading — no MPI runtime needed
#   bash scripts/build_jar_linux.sh MP   # multiprocessing — links system MPI
#
# The native library builds in the container bindings/jni/Dockerfile describes (glibc 2.28, GCC 14, static hwloc),
# so it loads on RHEL 8 and newer, and Docker must be available. Maven then packages the JAR on the host. Each
# variant gets its own .build-jni-${variant}/ directory so the two CMake caches don't collide.

set -e

usage() {
    echo "Usage: $(basename "$0") MT|MP" >&2
    exit 2
}

case "${1:-}" in
    MT) variant=multithreading;  mp_flag=OFF; flavor=mt     ;;
    MP) variant=multiprocessing; mp_flag=ON;  flavor=mp-mpi ;;
    *)  usage ;;
esac

# Fail fast on missing tools instead of letting `set -e` exit silently
# halfway through (the most common trap is a shell that has Docker but no
# Maven on PATH, which is exactly what happens when the only Maven on the
# host is IntelliJ-bundled).
for tool in docker mvn; do
    command -v "$tool" >/dev/null || {
        echo "Required tool '$tool' is not on PATH." >&2
        echo "Install it (or prepend its bin/ dir to PATH) and rerun." >&2
        exit 2
    }
done

if [[ "$(uname -m)" != x86_64 ]]; then
    echo "The build container is x86_64 only." >&2
    exit 2
fi

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
classifier=linux-x86_64
build_dir="$WORKSPACE/.build-jni-${variant}"
native_lib="$WORKSPACE/bindings/java/src/main/resources/natives/${classifier}/gempba_jni.so"

# jni/CMakeLists.txt sends gempba_jni.so straight into the shared resources
# tree, so an MT and MP build of this script overwrite the same file.  If
# both build dirs already exist and the file mtime sits between MT's and
# MP's prerequisites, GNU make sees "output newer than inputs" and skips
# the relink — the JAR ends up packaging the previous variant's native.
# Wipe the file before each cmake build to guarantee a fresh link.
rm -f "$native_lib"

image=gempba-jni-build:linux
echo "Building $image (the first build takes a few minutes)"
docker build -q -t "$image" "$WORKSPACE/bindings/jni" >/dev/null

# shellcheck source=_docker_workspace.sh
source "$WORKSPACE/scripts/_docker_workspace.sh"
set_docker_workspace_mounts "$WORKSPACE"

docker run --rm "${docker_workspace_mounts[@]}" --user "$(id -u):$(id -g)" -e HOME=/tmp -w "$WORKSPACE" "$image" bash -c "
    set -e
    cmake -B '$build_dir' -S '$WORKSPACE' \
        -DCMAKE_BUILD_TYPE=Release \
        -DGEMPBA_BUILD_JAVA_BINDING=ON \
        -DGEMPBA_JNI_CLASSIFIER=$classifier \
        -DGEMPBA_MULTIPROCESSING=$mp_flag \
        -DGEMPBA_HWLOC=ON \
        -DGEMPBA_DEV_MODE=OFF \
        -DGEMPBA_BUILD_TESTS=OFF \
        -DGEMPBA_DEBUG_COMMENTS=OFF
    cmake --build '$build_dir' --target gempba_jni -j\$(nproc)
    bash scripts/check_jni_deps.sh '$native_lib' $flavor
"

cd "$WORKSPACE/bindings/java"
# `install` (not `package`) so the produced JAR also lands in
# ~/.m2/repository/io/gempba/gempba/<version>/ — that's what a
# downstream <dependency> on this artifact resolves against.
mvn install -DskipTests -Dgempba.variant="$variant"
