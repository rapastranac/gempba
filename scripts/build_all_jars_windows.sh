#!/bin/bash
# Build both the MT and MP JARs in one invocation.
#
# Usage (from an MSYS2 MINGW64 shell):
#   bash scripts/build_all_jars_windows.sh
#
# Outputs:
#   bindings/java/target/gempba-<version>-mt.jar
#   bindings/java/target/gempba-<version>-mp-mpi.jar

set -e

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)

bash "$WORKSPACE/scripts/build_jar_windows.sh" MT
bash "$WORKSPACE/scripts/build_jar_windows.sh" MP
