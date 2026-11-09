#!/bin/bash
# Run the Java tests for one variant in a clean container that holds only the
# OS, a JDK and Maven, plus OpenMPI's runtime for MP, so they fail when the
# Linux native library needs anything else.
#
# Usage:
#   bash scripts/test_java_linux_clean.sh MT
#   bash scripts/test_java_linux_clean.sh MP
#
# Expects the variant's gempba_jni.so under
# bindings/java/src/main/resources/natives/linux-x86_64/.

set -e

usage() {
    echo "Usage: $(basename "$0") MT|MP" >&2
    exit 2
}

case "${1:-}" in
    MT) variant=multithreading;  mpi_runtime=""             ;;
    MP) variant=multiprocessing; mpi_runtime=libopenmpi3t64 ;;
    *)  usage ;;
esac

command -v docker >/dev/null || {
    echo "Required tool 'docker' is not on PATH." >&2
    exit 2
}

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=_docker_workspace.sh
source "$WORKSPACE/scripts/_docker_workspace.sh"
set_docker_workspace_mounts "$WORKSPACE"

image=maven:3.9-eclipse-temurin-25@sha256:93b8a14ea2f412782e4e842651273b4d903e35cc496284f178fbbe2d67d00976

docker run --rm "${docker_workspace_mounts[@]}" -w "$WORKSPACE/bindings/java" "$image" bash -c "
    set -e
    trap 'chown -R $(id -u):$(id -g) target 2>/dev/null || true' EXIT
    if [[ -n '$mpi_runtime' ]]; then
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y -qq $mpi_runtime >/dev/null
    fi
    mvn -B test -Dgempba.variant=$variant
"
