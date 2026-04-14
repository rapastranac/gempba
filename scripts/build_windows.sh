#!/bin/bash

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake -G "MSYS Makefiles" "$WORKSPACE" && make -j 20)