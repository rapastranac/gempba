#!/bin/bash
# Build the static dependencies the JNI library links on Linux and macOS: hwloc, and fmt and spdlog for their headers.
#
# Usage:
#   bash bindings/jni/build_deps.sh <prefix>
#
# Installs into <prefix> and does nothing when <prefix> already holds these versions. Set MACOSX_DEPLOYMENT_TARGET
# before calling on macOS so the archives target the same macOS as the library.

set -euo pipefail

prefix=${1:?"Usage: $(basename "$0") <prefix>"}

hwloc_version=2.13.0
hwloc_sha256=1514a5253f0a5c23bc006d3bdd30a6f6125c9a8dc9b5fa4984913d1fff45315d
fmt_version=12.1.0
fmt_sha256=ea7de4299689e12b6dddd392f9896f08fb0777ac7168897a244a6d6085043fea
spdlog_version=1.17.0
spdlog_sha256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744

stamp="$prefix/.gempba-jni-deps-hwloc-$hwloc_version-fmt-$fmt_version-spdlog-$spdlog_version"
if [[ -f "$stamp" ]]; then
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
jobs=$(getconf _NPROCESSORS_ONLN)

fetch() {
    local name=$1 url=$2 sha256=$3
    curl -fsSL -o "$work/$name.tar.gz" "$url"
    if command -v sha256sum >/dev/null; then
        echo "$sha256  $work/$name.tar.gz" | sha256sum -c --quiet -
    else
        echo "$sha256  $work/$name.tar.gz" | shasum -a 256 -c --quiet -
    fi
    mkdir -p "$work/$name"
    tar -xzf "$work/$name.tar.gz" -C "$work/$name" --strip-components=1
}

fetch hwloc "https://download.open-mpi.org/release/hwloc/v${hwloc_version%.*}/hwloc-$hwloc_version.tar.gz" "$hwloc_sha256"
(
    cd "$work/hwloc"
    ./configure --prefix="$prefix" --enable-static --disable-shared --with-pic \
        --disable-io --disable-libxml2 --disable-cairo --disable-libudev --disable-plugins --without-x
    make -j"$jobs" SUBDIRS="include hwloc"
    make install SUBDIRS="include hwloc"
)

fetch fmt "https://github.com/fmtlib/fmt/archive/refs/tags/$fmt_version.tar.gz" "$fmt_sha256"
cmake -S "$work/fmt" -B "$work/fmt-build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DFMT_TEST=OFF -DFMT_DOC=OFF
cmake --build "$work/fmt-build" -j"$jobs"
cmake --install "$work/fmt-build"

fetch spdlog "https://github.com/gabime/spdlog/archive/refs/tags/v$spdlog_version.tar.gz" "$spdlog_sha256"
cmake -S "$work/spdlog" -B "$work/spdlog-build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_PREFIX_PATH="$prefix" -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DSPDLOG_FMT_EXTERNAL=ON \
    -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_INSTALL=ON
cmake --build "$work/spdlog-build" -j"$jobs"
cmake --install "$work/spdlog-build"

touch "$stamp"
