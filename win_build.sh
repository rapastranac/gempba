#!/bin/bash

WORKSPACE=$(dirname "$0")
mkdir -p "$WORKSPACE/build"
(cd "$WORKSPACE/build" && cmake -G "MSYS Makefiles" .. && make -j 20)