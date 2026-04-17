#!/usr/bin/env bash
# update-pkgbuild.sh <version>
#
# Updates packaging/msys2/PKGBUILD with the new pkgver and the sha256 of the
# corresponding GitHub release tarball.
#
# Run AFTER the tag v<version> exists on GitHub (the tarball must be available).
# The repo is derived automatically from the git remote, so this works for both
# the private org repo and the public repo without any changes.
#
# Usage:
#   ./scripts/update-pkgbuild.sh 3.1.0

set -euo pipefail

VERSION="${1:-}"
if [[ -z "$VERSION" ]]; then
    echo "Usage: $0 <version>" >&2
    exit 1
fi

PKGBUILD="packaging/msys2/PKGBUILD"

if [[ ! -f "$PKGBUILD" ]]; then
    echo "Error: $PKGBUILD not found. Run from the repo root." >&2
    exit 1
fi

# Derive owner/repo from the git remote (works for private and public repos)
REMOTE_URL=$(git remote get-url origin)
REPO=$(echo "$REMOTE_URL" \
    | sed 's|.*github\.com[:/]\(.*\)\.git|\1|' \
    | sed 's|.*github\.com[:/]\(.*\)|\1|')

echo "Repo: ${REPO}"
echo "Tag:  v${VERSION}"

# Update pkgver
sed -i "s/^pkgver=.*/pkgver=${VERSION}/" "$PKGBUILD"
echo "pkgver set to ${VERSION}"

# Fetch the tarball via gh CLI (handles auth for private repos) and compute sha256
echo "Fetching tarball..."
TMPFILE=$(mktemp)
trap 'rm -f "$TMPFILE"' EXIT

if gh api "repos/${REPO}/tarball/v${VERSION}" > "$TMPFILE" 2>/dev/null && [[ -s "$TMPFILE" ]]; then
    SHA256=$(sha256sum "$TMPFILE" | awk '{print $1}')
    sed -i "s|^sha256sums=.*|sha256sums=('${SHA256}')|" "$PKGBUILD"
    echo "sha256sums set to ${SHA256}"
else
    sed -i "s|^sha256sums=.*|sha256sums=('SKIP')|" "$PKGBUILD"
    echo "Warning: tag v${VERSION} not found on GitHub. sha256sums set to SKIP." >&2
    echo "Push the tag first, then rerun this script." >&2
    exit 1
fi
