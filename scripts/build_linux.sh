#!/bin/bash

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
BUILD_TYPE="${1:-Debug}"
mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake -DCMAKE_BUILD_TYPE="$BUILD_TYPE" "$WORKSPACE" && make -j 20)
