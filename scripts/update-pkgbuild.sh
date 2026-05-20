#!/usr/bin/env bash
# update-pkgbuild.sh <version> [-r|--remote <name>]
#
# Updates packaging/msys2/PKGBUILD with the new pkgver and the sha256 of the
# corresponding GitHub release tarball.
#
# Run AFTER the tag v<version> exists on GitHub for the chosen remote (the
# tarball must be fetchable). The owner/repo is derived from the named git
# remote (default 'origin'); pass --remote to point at a different one
# (typically 'upstream' so PKGBUILD's sha matches the public release artifact).
#
# Usage:
#   ./scripts/update-pkgbuild.sh 3.1.0
#   ./scripts/update-pkgbuild.sh 3.1.0 --remote upstream
#   ./scripts/update-pkgbuild.sh 3.1.0 -r upstream

set -euo pipefail

REMOTE="origin"
VERSION=""
while [[ $# -gt 0 ]]; do
    case "$1" in
        -r|--remote)   REMOTE="${2:-}"; shift 2 ;;
        --remote=*)    REMOTE="${1#*=}"; shift ;;
        -h|--help)     sed -n '2,15p' "$0"; exit 0 ;;
        --)            shift; break ;;
        -*)            echo "Unknown flag: $1" >&2; exit 1 ;;
        *)
            if [[ -z "$VERSION" ]]; then VERSION="$1"; shift
            else echo "Unexpected argument: $1" >&2; exit 1; fi ;;
    esac
done

if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 <version> [-r|--remote <name>]" >&2
    exit 1
fi

PKGBUILD="packaging/msys2/PKGBUILD"

if [[ ! -f "$PKGBUILD" ]]; then
    echo "Error: $PKGBUILD not found. Run from the repo root." >&2
    exit 1
fi

# Derive owner/repo from the selected git remote
if ! REMOTE_URL=$(git remote get-url "$REMOTE" 2>/dev/null); then
    echo "Error: git remote '$REMOTE' does not exist. Configured remotes:" >&2
    git remote -v >&2
    exit 1
fi
REPO=$(echo "$REMOTE_URL" \
    | sed 's|.*github\.com[:/]\(.*\)\.git|\1|' \
    | sed 's|.*github\.com[:/]\(.*\)|\1|')

echo "Remote: ${REMOTE}"
echo "Repo:   ${REPO}"
echo "Tag:    v${VERSION}"

# Update pkgver
sed -i "s/^pkgver=.*/pkgver=${VERSION}/" "$PKGBUILD"
echo "pkgver set to ${VERSION}"

# Fetch the source tarball and compute sha256. Try the public archive URL first
# (byte-identical to what PKGBUILD source=() actually resolves to). Fall back to
# `gh api` so this still works for private repos.
echo "Fetching tarball..."
TMPFILE=$(mktemp)
ERR_FILE=$(mktemp)
trap 'rm -f "$TMPFILE" "$ERR_FILE"' EXIT

TARBALL_URL="https://github.com/${REPO}/archive/refs/tags/v${VERSION}.tar.gz"
FETCHED_VIA=""

if curl -fsSL --retry 2 "$TARBALL_URL" -o "$TMPFILE" 2>"$ERR_FILE" && [[ -s "$TMPFILE" ]]; then
    FETCHED_VIA="curl ($TARBALL_URL)"
elif gh api "repos/${REPO}/tarball/v${VERSION}" > "$TMPFILE" 2>"$ERR_FILE" && [[ -s "$TMPFILE" ]]; then
    FETCHED_VIA="gh api repos/${REPO}/tarball/v${VERSION}"
else
    sed -i "s|^sha256sums=.*|sha256sums=('SKIP')|" "$PKGBUILD"
    echo "Error: could not fetch tarball for v${VERSION} from ${REPO}" >&2
    echo "  Tried: ${TARBALL_URL}" >&2
    echo "  Tried: gh api repos/${REPO}/tarball/v${VERSION}" >&2
    if [[ -s "$ERR_FILE" ]]; then
        echo "  Last error:" >&2
        sed 's/^/    /' "$ERR_FILE" >&2
    fi
    echo "sha256sums set to SKIP. Verify the tag exists for the selected remote." >&2
    exit 1
fi

SHA256=$(sha256sum "$TMPFILE" | awk '{print $1}')
sed -i "s|^sha256sums=.*|sha256sums=('${SHA256}')|" "$PKGBUILD"
echo "sha256sums set to ${SHA256}"
echo "  (fetched via: ${FETCHED_VIA})"
