#!/bin/bash

WORKSPACE=$(dirname "$0")
mkdir -p "$WORKSPACE/build"
(cd "$WORKSPACE/build" && cmake .. && make -j 20)