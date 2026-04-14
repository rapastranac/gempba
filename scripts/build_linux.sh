#!/bin/bash

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$WORKSPACE/.build"
(cd "$WORKSPACE/.build" && cmake "$WORKSPACE" && make -j 20)