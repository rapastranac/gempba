#!/bin/bash
# Assembles the Maven Central bundle of gempba's Java artifacts (#410), then checks it:
#   io.github.rapastranac:gempba      from gempba-<version>-mt.jar
#   io.github.rapastranac:gempba-mpi  from gempba-<version>-mp-mpi.jar
# each with its POM and its sources and javadoc JARs, every file signed with the default GPG key and given .md5 and
# .sha1 checksums, zipped in Maven's repository layout as <output dir>/gempba-<version>-central-bundle.zip.
#
# Usage:
#   bash scripts/central_bundle.sh <dir holding the two variant JARs> <output dir>
#
# GPG_PASSPHRASE, when set, unlocks the signing key. Needs bash, gpg, md5sum, sha1sum, Maven and a JDK (jar).

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $(basename "$0") <dir holding the two variant JARs> <output dir>" >&2
    exit 2
fi
for tool in gpg md5sum sha1sum mvn jar; do
    command -v "$tool" >/dev/null || {
        echo "Required tool '$tool' is not on PATH." >&2
        exit 2
    }
done

WORKSPACE=$(cd "$(dirname "$0")/.." && pwd)
# shellcheck source=_java_artifact.sh
source "$WORKSPACE/scripts/_java_artifact.sh"

version=$(gempba_java_version "$WORKSPACE")
if [[ $version == *-SNAPSHOT ]]; then
    echo "Maven Central rejects SNAPSHOT versions ($version)." >&2
    exit 1
fi

mkdir -p "$2"
out=$(cd "$2" && pwd)
rm -rf "$out/input" "$out/bundle"
mkdir -p "$out/input" "$out/bundle"
# The variant builds below clean bindings/java/target, which may be where the JARs came from.
for classifier in mt mp-mpi; do
    cp "$1/gempba-$version-$classifier.jar" "$out/input/"
done

sign_args=(--batch --yes --armor --detach-sign)
if [[ -n ${GPG_PASSPHRASE:-} ]]; then
    sign_args+=(--pinentry-mode loopback --passphrase "$GPG_PASSPHRASE")
fi

for classifier in mt mp-mpi; do
    case "$classifier" in
        mt)     variant=multithreading ;;
        mp-mpi) variant=multiprocessing ;;
    esac
    artifact_id=$(gempba_java_artifact_id "$classifier")
    dir="$out/bundle/io/github/rapastranac/$artifact_id/$version"
    base="$dir/$artifact_id-$version"
    mkdir -p "$dir"

    echo "Building $artifact_id's sources and javadoc JARs"
    (cd "$WORKSPACE/bindings/java" && mvn -B -q clean package -DskipTests -Dgempba.variant="$variant" -Ppublish)
    cp "$out/input/gempba-$version-$classifier.jar"                                  "$base.jar"
    cp "$WORKSPACE/bindings/java/target/gempba-$version-$classifier-sources.jar"     "$base-sources.jar"
    cp "$WORKSPACE/bindings/java/target/gempba-$version-$classifier-javadoc.jar"     "$base-javadoc.jar"
    gempba_java_write_pom "$WORKSPACE" "$classifier" "$base.pom"

    for file in "$base.jar" "$base.pom" "$base-sources.jar" "$base-javadoc.jar"; do
        gpg "${sign_args[@]}" --output "$file.asc" "$file"
        md5sum "$file" | cut -d' ' -f1 > "$file.md5"
        sha1sum "$file" | cut -d' ' -f1 > "$file.sha1"
    done
done

# ─── Check what Central validates, and that each JAR is its variant ─────────
failures=0
fail() {
    echo "  FAILED: $1" >&2
    failures=$((failures + 1))
}
for classifier in mt mp-mpi; do
    artifact_id=$(gempba_java_artifact_id "$classifier")
    base="$out/bundle/io/github/rapastranac/$artifact_id/$version/$artifact_id-$version"
    echo "Checking $artifact_id $version"
    for file in "$base.jar" "$base.pom" "$base-sources.jar" "$base-javadoc.jar"; do
        [[ -s $file ]] || { fail "$(basename "$file") is missing or empty"; continue; }
        gpg --batch --verify "$file.asc" "$file" 2>/dev/null || fail "$(basename "$file").asc doesn't verify"
        [[ $(md5sum "$file" | cut -d' ' -f1) == $(cat "$file.md5") ]] || fail "$(basename "$file").md5 doesn't match"
        [[ $(sha1sum "$file" | cut -d' ' -f1) == $(cat "$file.sha1") ]] || fail "$(basename "$file").sha1 doesn't match"
    done
    for element in "<groupId>io.github.rapastranac</groupId>" "<artifactId>$artifact_id</artifactId>" "<version>$version</version>" \
        "<name>" "<description>" "<url>" "<licenses>" "<developers>" "<scm>"; do
        grep -q "$element" "$base.pom" || fail "the POM has no $element"
    done
    entries=$(jar tf "$base.jar")
    grep -q '^natives/' <<< "$entries" || fail "$artifact_id's JAR has no native library"
    if [[ $classifier == mt ]]; then
        ! grep -q '^io/gempba/mpi/' <<< "$entries" || fail "$artifact_id's JAR has io.gempba.mpi"
    else
        grep -q '^io/gempba/mpi/Mpi.class$' <<< "$entries" || fail "$artifact_id's JAR has no io.gempba.mpi.Mpi"
    fi
done
if [[ $failures -ne 0 ]]; then
    echo "The bundle has $failures problem(s)." >&2
    exit 1
fi

zip="$out/gempba-$version-central-bundle.zip"
rm -f "$zip"
(cd "$out/bundle" && jar --create --no-manifest --file "$zip" io)
echo "Bundle: $zip"
jar tf "$zip" | grep -v '/$' | sort
