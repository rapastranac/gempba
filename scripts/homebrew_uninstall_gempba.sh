#!/usr/bin/env bash
# Uninstalls every installed gempba Homebrew flavor, and fails if one is still installed afterwards.
#
# The flavors conflict with each other, so the Homebrew verify job runs this on its persistent macOS
# runner before installing a flavor and again at cleanup.
#
# Usage:
#   scripts/homebrew_uninstall_gempba.sh

set -euo pipefail

CELLAR="$(brew --cellar)"
FLAVORS=(gempba gempba-mpi)

for FLAVOR in "${FLAVORS[@]}"; do
    if [ -d "$CELLAR/$FLAVOR" ]; then
        brew uninstall --force --ignore-dependencies "$FLAVOR"
    fi
done

for FLAVOR in "${FLAVORS[@]}"; do
    if [ -d "$CELLAR/$FLAVOR" ]; then
        echo "::error::$FLAVOR is still installed in $CELLAR; uninstall it on this runner before rerunning" >&2
        exit 1
    fi
done
