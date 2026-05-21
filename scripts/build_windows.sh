#!/bin/bash

# Call this script like this:
# bash scripts/build_windows.sh [BUILD_TYPE] [HWLOC]
#
# Examples:
#   bash scripts/build_windows.sh                  # Debug, hwloc ON  (defaults)
#   bash scripts/build_windows.sh Release          # Release, hwloc ON
#   bash scripts/build_windows.sh Debug OFF        # Debug, hwloc OFF

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
BUILD_TYPE="${1:-Debug}"
HWLOC="${2:-ON}"

mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake -G "MSYS Makefiles" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DGEMPBA_HWLOC="$HWLOC" \
    -DGEMPBA_BUILD_TESTS=ON \
    -DGEMPBA_BUILD_EXAMPLES=ON \
    "$WORKSPACE" && make -j"$(nproc)")
