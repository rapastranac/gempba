#!/bin/bash

# Call this script like this:
# bash scripts/build_linux.sh [BUILD_TYPE] [HWLOC]
#
# Examples:
#   bash scripts/build_linux.sh                  # Debug, hwloc ON  (defaults)
#   bash scripts/build_linux.sh Release          # Release, hwloc ON
#   bash scripts/build_linux.sh Debug OFF        # Debug, hwloc OFF

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
BUILD_TYPE="${1:-Debug}"
HWLOC="${2:-ON}"

mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DGEMPBA_HWLOC="$HWLOC" \
    -DGEMPBA_BUILD_TESTS=ON \
    "$WORKSPACE" && make -j"$(nproc)")
