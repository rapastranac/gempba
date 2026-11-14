#!/bin/bash
# Install gempba, built without MPI, in a clean Debian 13 container that has MPICH and no Open MPI. Then build tests/install
# against that installation and run it on 4 ranks, so gempba::mpi compiles from its installed sources against MPICH (#415).
# Debian, because Ubuntu 24.04's MPICH starts every rank of a job alone (Launchpad bug 2072338).
#
# Usage:
#   bash scripts/test_mpich_consumer.sh

set -e

command -v docker >/dev/null || {
    echo "Required tool 'docker' is not on PATH." >&2
    exit 2
}

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=_docker_workspace.sh
source "$WORKSPACE/scripts/_docker_workspace.sh"
set_docker_workspace_mounts "$WORKSPACE"

image=debian:trixie@sha256:9c6e381421d0f48d1b8133e573f33c185ee16cbf505eda9c4a8a4791b25ea8a6

docker run --rm "${docker_workspace_mounts[@]}" -w "$WORKSPACE" "$image" bash -c '
    set -e
    export DEBIAN_FRONTEND=noninteractive CC=gcc-14 CXX=g++-14
    apt-get update -qq
    apt-get install -y -qq ca-certificates git cmake make g++-14 pkg-config libspdlog-dev libfmt-dev libhwloc-dev libmpich-dev >/dev/null

    cmake -S . -B /tmp/gempba-build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/tmp/gempba \
        -DGEMPBA_BUILD_TESTS=OFF -DGEMPBA_BUILD_MPI=OFF >/dev/null
    cmake --build /tmp/gempba-build --parallel "$(nproc)" >/dev/null
    cmake --install /tmp/gempba-build >/dev/null
    compiled=$(find /tmp/gempba -name "*gempba_mpi*" \( -name "*.so*" -o -name "*.a" \))
    if [[ -n "$compiled" ]]; then
        echo "The installation holds compiled MPI code: $compiled" >&2
        exit 1
    fi

    cmake -S tests/install -B /tmp/consumer -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/tmp/gempba >/dev/null
    grep -E "^MPI_CXX_LIB_NAMES:" /tmp/consumer/CMakeCache.txt
    cmake --build /tmp/consumer --parallel "$(nproc)" >/dev/null
    test -f /tmp/consumer/libgempba_mpi.a

    for topology in semi_centralized centralized; do
        mpiexec.mpich -n 4 /tmp/consumer/termination_stress 14 "$topology"
    done
'
