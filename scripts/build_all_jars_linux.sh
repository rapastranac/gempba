#!/bin/bash
# Build both the MT and MP JARs in one invocation.
#
# Usage:
#   bash scripts/build_all_jars_linux.sh
#
# Outputs:
#   bindings/java/target/gempba-<version>-mt.jar
#   bindings/java/target/gempba-<version>-mp-mpi.jar

set -e

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)

bash "$WORKSPACE/scripts/build_jar_linux.sh" MT
bash "$WORKSPACE/scripts/build_jar_linux.sh" MP
