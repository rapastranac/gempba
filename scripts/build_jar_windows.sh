#!/bin/bash
# Build the gempba Windows JAR for one variant.
#
# Usage (from an MSYS2 MINGW64 shell):
#   bash scripts/build_jar_windows.sh MT   # multithreading — no MPI runtime needed
#   bash scripts/build_jar_windows.sh MP   # multiprocessing — links MS-MPI
#
# Each variant gets its own .build-${variant}/ directory so the two CMake
# caches don't collide.

set -e

usage() {
    echo "Usage: $(basename "$0") MT|MP" >&2
    exit 2
}

case "${1:-}" in
    MT) variant=multithreading;  mp_flag=OFF ;;
    MP) variant=multiprocessing; mp_flag=ON  ;;
    *)  usage ;;
esac

# MSYS2 with `path-type: minimal` (the default) strips Windows PATH down
# to system dirs, so Windows-installed apps under Program Files (Apache
# Maven, Eclipse Adoptium JDK, …) are NOT visible from this shell.  cmd's
# `where` and `echo %VAR%` would inherit the same stripped env and miss
# them too.  PowerShell reads the registry-stored Machine/User env
# directly, which is unaffected — use it to discover mvn / JAVA_HOME
# when bash doesn't already know about them.
#
# cmake is expected to come from the MSYS2 mingw-w64 toolchain
# (consistent with build_windows.sh), so we don't try to inherit that.
ps_env() {
    powershell.exe -NoProfile -Command \
        "[Environment]::GetEnvironmentVariable('$1','Machine'); [Environment]::GetEnvironmentVariable('$1','User')" \
        2>/dev/null | tr -d '\r' | grep -v '^$' | head -1
}

if [[ -z "${JAVA_HOME:-}" ]]; then
    win_jh=$(ps_env JAVA_HOME)
    if [[ -n "$win_jh" ]]; then
        export JAVA_HOME="$win_jh"
    else
        echo "JAVA_HOME not set in MSYS2 or in Windows (Machine/User env)." >&2
        echo "Install a JDK and set JAVA_HOME (Windows env vars are fine)." >&2
        exit 2
    fi
fi

if ! command -v mvn >/dev/null; then
    win_path=$(powershell.exe -NoProfile -Command \
        "[Environment]::GetEnvironmentVariable('PATH','Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH','User')" \
        2>/dev/null | tr -d '\r')
    mvn_dir=""
    IFS=';' read -ra entries <<< "$win_path"
    for e in "${entries[@]}"; do
        [[ -z "$e" ]] && continue
        unix_e=$(cygpath -u "$e" 2>/dev/null) || continue
        if [[ -f "$unix_e/mvn.cmd" ]]; then
            mvn_dir=$unix_e
            break
        fi
    done
    if [[ -n "$mvn_dir" ]]; then
        export PATH="$mvn_dir:$PATH"
    else
        echo "Apache Maven not found in Windows PATH (Machine or User)." >&2
        echo "Install Maven from https://maven.apache.org/download.cgi and" >&2
        echo "add its bin/ to Windows PATH (no MSYS2 changes needed)." >&2
        exit 2
    fi
fi

if ! command -v cmake >/dev/null; then
    echo "cmake is not on PATH (MSYS2 shell)." >&2
    echo "Install via 'pacman -S mingw-w64-x86_64-cmake' and rerun." >&2
    exit 2
fi

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
classifier=win-x86_64
build_dir="$WORKSPACE/.build-${variant}"
native_dir="$WORKSPACE/bindings/java/src/main/resources/natives/${classifier}"
native_lib="$native_dir/gempba_jni.dll"

JAVA_HOME_UNIX=$(cygpath -u "$JAVA_HOME")

# jni/CMakeLists.txt sends gempba_jni.dll straight into the shared resources
# tree, so an MT and MP build of this script overwrite the same file.  If
# both build dirs already exist and the file mtime sits between MT's and
# MP's prerequisites, GNU make sees "output newer than inputs" and skips
# the relink — the JAR ends up packaging the previous variant's native
# (specifically, an MP-flavored .dll inside an MT JAR will pull in
# msmpi.dll at runtime and fail with "specified procedure could not be
# found" on hosts without MS-MPI).  Wipe the file before each cmake build
# to guarantee a fresh link.
rm -f "$native_lib"

cmake -B "$build_dir" -S "$WORKSPACE" \
    -G "MSYS Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DGEMPBA_BUILD_JAVA_BINDING=ON \
    -DGEMPBA_JNI_CLASSIFIER="$classifier" \
    -DGEMPBA_MULTIPROCESSING="$mp_flag" \
    -DGEMPBA_HWLOC=ON \
    -DGEMPBA_DEV_MODE=OFF \
    -DGEMPBA_BUILD_TESTS=OFF \
    -DGEMPBA_DEBUG_COMMENTS=OFF \
    -DJAVA_HOME="$JAVA_HOME_UNIX"

cmake --build "$build_dir" --target gempba_jni -j"$(nproc)"

cd "$WORKSPACE/bindings/java"
# `install` (not `package`) so the produced JAR also lands in
# ~/.m2/repository/io/gempba/gempba/<version>/ — that's what a
# downstream <dependency> on this artifact resolves against.
mvn install -DskipTests -Dgempba.variant="$variant"
