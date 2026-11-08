#!/bin/bash
# Side-effect-free helper library: defines a function, does nothing on its own.
# Source from bash; do not execute directly.
#
# Used by:
#   scripts/build_jar_linux.sh
#   scripts/test_java_linux_clean.sh

# Sets docker_workspace_mounts to the `docker run` options that show the
# directory $1 at the same path inside a container. On a runner that itself
# runs in a container, the directory lives in that container's volumes, so
# those are mounted instead of a host path that doesn't exist.
set_docker_workspace_mounts() {
    if [[ -f /.dockerenv ]]; then
        docker_workspace_mounts=(--volumes-from "$(hostname)")
    else
        docker_workspace_mounts=(-v "$1:$1")
    fi
}
