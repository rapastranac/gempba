#!/bin/bash
# Side-effect-free helper library: defines functions, does nothing on its own.
# Source from bash; do not execute directly.
#
# Each Java variant is published as its own artifact (#410): the mt JAR as io.github.rapastranac:gempba and the
# mp-<backend> JAR as io.github.rapastranac:gempba-<backend>. These functions give local installs and Maven Central
# the same coordinates.
#
# Used by:
#   scripts/build_jar_linux.sh, scripts/build_jar_macos.sh, scripts/build_jar_windows.sh
#   scripts/central_bundle.sh

# Prints the version in bindings/java/pom.xml. $1: the workspace root.
gempba_java_version() {
    grep -m1 '<version>' "$1/bindings/java/pom.xml" | sed -E 's/[[:space:]]*<version>([^<]+)<\/version>.*/\1/' | tr -d '\r'
}

# Prints the published artifactId of a variant's classifier. $1: the classifier (mt, mp-mpi).
gempba_java_artifact_id() {
    case "$1" in
        mt)   echo gempba ;;
        mp-*) echo "gempba-${1#mp-}" ;;
        *)    echo "Unknown classifier '$1'." >&2; return 1 ;;
    esac
}

# Writes the published POM of a classifier's artifact: bindings/java/pom.xml with that artifactId, name and description.
# $1: the workspace root, $2: the classifier, $3: the output file.
gempba_java_write_pom() {
    local workspace=$1 classifier=$2 out=$3 artifact_id component
    artifact_id=$(gempba_java_artifact_id "$classifier") || return 1
    component=""
    if [[ $classifier == mp-* ]]; then
        component=$(printf '%s' "${classifier#mp-}" | tr '[:lower:]' '[:upper:]')
    fi
    awk -v id="$artifact_id" -v component="$component" '
        !done_id && /<artifactId>gempba<\/artifactId>/ {
            sub(/<artifactId>gempba<\/artifactId>/, "<artifactId>" id "</artifactId>"); done_id = 1
        }
        !done_name && /<name>gempba<\/name>/ {
            sub(/<name>gempba<\/name>/, "<name>" id "</name>"); done_name = 1
        }
        !done_description && component != "" && /<\/description>/ {
            sub(/<\/description>/, ", with its " component " component</description>"); done_description = 1
        }
        { print }
    ' "$workspace/bindings/java/pom.xml" > "$out"
}

# Builds a variant's JAR in bindings/java and installs it into ~/.m2 under its published coordinates, where a downstream
# <dependency> resolves it. $1: the workspace root, $2: the variant (multithreading, multiprocessing), $3: its classifier.
gempba_java_install() {
    local workspace=$1 variant=$2 classifier=$3 version pom
    version=$(gempba_java_version "$workspace")
    pom="target/$(gempba_java_artifact_id "$classifier")-$version.pom"
    (cd "$workspace/bindings/java" && mvn -B clean package -DskipTests -Dgempba.variant="$variant") || return 1
    gempba_java_write_pom "$workspace" "$classifier" "$workspace/bindings/java/$pom" || return 1
    # Paths relative to bindings/java: on Windows, Maven can't read the MSYS paths bash uses.
    (cd "$workspace/bindings/java" && mvn -B install:install-file -Dfile="target/gempba-$version-$classifier.jar" -DpomFile="$pom")
}
