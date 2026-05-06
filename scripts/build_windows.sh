#!/bin/bash

# Call this script like this:
# bash scripts/build_windows.sh [BUILD_TYPE] [TELEMETRY] [HWLOC]
#
# Examples:
#   bash scripts/build_windows.sh                  # Debug, telemetry ON, hwloc ON  (defaults)
#   bash scripts/build_windows.sh Release          # Release, telemetry ON, hwloc ON
#   bash scripts/build_windows.sh Debug OFF        # Debug, telemetry OFF, hwloc ON
#   bash scripts/build_windows.sh Debug ON OFF     # Debug, telemetry ON, hwloc OFF

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
BUILD_TYPE="${1:-Debug}"
TELEMETRY="${2:-ON}"
HWLOC="${3:-ON}"

mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake -G "MSYS Makefiles" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DGEMPBA_TELEMETRY="$TELEMETRY" \
    -DGEMPBA_HWLOC="$HWLOC" \
    -DGEMPBA_BUILD_TESTS=ON \
    -DGEMPBA_BUILD_EXAMPLES=ON \
    "$WORKSPACE" && make -j"$(nproc)")
