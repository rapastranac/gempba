#!/usr/bin/env bash
# Lints the repository using .clang-format and .clang-tidy.
#
# Usage:
#   scripts/lint.sh                  # run both clang-format (check) and clang-tidy
#   scripts/lint.sh --format-only    # only clang-format check
#   scripts/lint.sh --tidy-only      # only clang-tidy
#   scripts/lint.sh --fix            # apply clang-format changes and clang-tidy fixes
#   scripts/lint.sh --jobs N         # parallelism (default: nproc)
#
# Environment overrides:
#   CLANG_FORMAT   path to clang-format binary   (default: clang-format)
#   CLANG_TIDY     path to clang-tidy binary     (default: clang-tidy)
#   RUN_CLANG_TIDY path to run-clang-tidy helper (default: run-clang-tidy)
#   BUILD_DIR      CMake build directory used for compile_commands.json
#                  (default: <repo>/.build-lint)

set -euo pipefail

WORKSPACE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$WORKSPACE"

BUILD_DIR="${BUILD_DIR:-$WORKSPACE/.build-lint}"

resolve_tool() {
    local override="$1" base="$2"
    if [[ -n "$override" ]]; then
        echo "$override"
        return
    fi
    if command -v "$base" >/dev/null 2>&1; then
        echo "$base"
        return
    fi
    local best
    best="$(compgen -c "$base-" 2>/dev/null \
        | grep -E "^${base}-[0-9]+$" \
        | sort -t- -k2 -n -r \
        | head -1)"
    echo "${best:-$base}"
}

CLANG_FORMAT="$(resolve_tool "${CLANG_FORMAT:-}" clang-format)"
CLANG_TIDY="$(resolve_tool "${CLANG_TIDY:-}" clang-tidy)"
RUN_CLANG_TIDY="$(resolve_tool "${RUN_CLANG_TIDY:-}" run-clang-tidy)"
CLANG_APPLY_REPLACEMENTS="$(resolve_tool "${CLANG_APPLY_REPLACEMENTS:-}" clang-apply-replacements)"

DO_FORMAT=1
DO_TIDY=1
FIX=0
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || echo 4)"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --format-only) DO_TIDY=0 ;;
        --tidy-only)   DO_FORMAT=0 ;;
        --fix)         FIX=1 ;;
        --jobs)        JOBS="$2"; shift ;;
        -h|--help)
            sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
    shift
done

# Directories that hold first-party sources.
SOURCE_DIRS=(include src tests private)

# Collect all in-repo C/C++ files, excluding build and external trees.
mapfile -t SOURCE_FILES < <(
    find "${SOURCE_DIRS[@]}" \
        \( -name '*.cpp' -o -name '*.cxx' -o -name '*.cc' \
        -o -name '*.hpp' -o -name '*.hxx' -o -name '*.h' \) \
        -not -path '*/.*' \
        2>/dev/null | sort
)

if [[ ${#SOURCE_FILES[@]} -eq 0 ]]; then
    echo "No source files found under: ${SOURCE_DIRS[*]}" >&2
    exit 1
fi

echo "📁 Workspace: $WORKSPACE"
echo "📄 Files:     ${#SOURCE_FILES[@]}"
echo "🧵 Jobs:      $JOBS"
echo

EXIT_STATUS=0

# --------------------------------------------------------------------
# clang-format
# --------------------------------------------------------------------
if [[ $DO_FORMAT -eq 1 ]]; then
    echo "🎨 Running clang-format ($("$CLANG_FORMAT" --version 2>/dev/null | head -1))"
    if [[ $FIX -eq 1 ]]; then
        printf '%s\0' "${SOURCE_FILES[@]}" \
            | xargs -0 -n 32 -P "$JOBS" "$CLANG_FORMAT" -i --style=file
        echo "✅ clang-format applied in place"
    else
        if ! printf '%s\0' "${SOURCE_FILES[@]}" \
            | xargs -0 -n 32 -P "$JOBS" "$CLANG_FORMAT" --dry-run --Werror --style=file; then
            echo "❌ clang-format reported formatting issues" >&2
            EXIT_STATUS=1
        else
            echo "✅ clang-format clean"
        fi
    fi
    echo
fi

# --------------------------------------------------------------------
# clang-tidy
# --------------------------------------------------------------------
if [[ $DO_TIDY -eq 1 ]]; then
    echo "🧪 Running clang-tidy ($("$CLANG_TIDY" --version 2>/dev/null | head -2 | tail -1 | xargs))"

    # Generate compile_commands.json if missing. Use a dedicated build dir so
    # this does not clobber the user's regular build/ tree.
    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
        echo "⚙️  Configuring CMake in $BUILD_DIR (first run)"
        cmake -S "$WORKSPACE" -B "$BUILD_DIR" \
            -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
            -DGEMPBA_BUILD_TESTS=ON \
            -DGEMPBA_BUILD_EXAMPLES=ON \
            -DGEMPBA_MULTIPROCESSING=ON \
            >/dev/null
    fi

    if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
        echo "❌ compile_commands.json missing at $BUILD_DIR/" >&2
        exit 1
    fi

    # Restrict to in-repo files so we don't lint system / Boost / MPI headers.
    # On MSYS/Cygwin, compile_commands.json uses native Windows paths (C:/...)
    # while bash paths are /c/... — translate via cygpath so the regex matches.
    FILTER_ROOT="$WORKSPACE"
    if command -v cygpath >/dev/null 2>&1; then
        FILTER_ROOT="$(cygpath -m "$WORKSPACE")"
    fi
    FILE_FILTER="^$(printf '%s\n' "$FILTER_ROOT" | sed 's/[][\.*^$/]/\\&/g')/(include|src|tests|private)/"

    HEADER_FILTER="$(printf '%s\n' "$FILTER_ROOT" | sed 's/[][\.*^$/]/\\&/g')/(include|src|tests|private)/.*"

    TIDY_ARGS=(
        -p "$BUILD_DIR"
        -j "$JOBS"
        -quiet
        -clang-tidy-binary "$CLANG_TIDY"
        -header-filter="$HEADER_FILTER"
    )
    if [[ $FIX -eq 1 ]]; then
        TIDY_ARGS+=(-fix -clang-apply-replacements-binary "$CLANG_APPLY_REPLACEMENTS")
    fi

    if command -v "$RUN_CLANG_TIDY" >/dev/null 2>&1; then
        # run-clang-tidy exits 0 even when warnings are emitted; scan its
        # output for any ": warning:" / ": error:" line to detect findings.
        TIDY_LOG="$(mktemp)"
        "$RUN_CLANG_TIDY" "${TIDY_ARGS[@]}" "$FILE_FILTER" 2>&1 | tee "$TIDY_LOG"
        TIDY_RC=${PIPESTATUS[0]}
        if [[ $TIDY_RC -ne 0 ]] || grep -qE ': (warning|error): ' "$TIDY_LOG"; then
            echo "❌ clang-tidy reported issues" >&2
            EXIT_STATUS=1
        else
            echo "✅ clang-tidy clean"
        fi
        rm -f "$TIDY_LOG"
    else
        echo "ℹ️  run-clang-tidy not found; falling back to sequential clang-tidy"
        TIDY_FAILED=0
        for f in "${SOURCE_FILES[@]}"; do
            # Skip headers not in the compile DB; clang-tidy will still try but
            # warnings from headers surface via the .cpp that includes them.
            case "$f" in
                *.cpp|*.cxx|*.cc)
                    if ! "$CLANG_TIDY" -p "$BUILD_DIR" -quiet $([[ $FIX -eq 1 ]] && echo --fix) "$f"; then
                        TIDY_FAILED=1
                    fi
                    ;;
            esac
        done
        if [[ $TIDY_FAILED -eq 1 ]]; then
            echo "❌ clang-tidy reported issues" >&2
            EXIT_STATUS=1
        else
            echo "✅ clang-tidy clean"
        fi
    fi
    echo
fi

if [[ $EXIT_STATUS -eq 0 ]]; then
    echo "🎉 Lint passed"
else
    echo "💥 Lint failed"
fi
exit $EXIT_STATUS
