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

# ANSI styling. Order: NO_COLOR wins; FORCE_COLOR forces on; CI runners
# (GitHub Actions, generic CI=true) get colour because their log viewer
# renders ANSI even though stdout is not a TTY; otherwise honour TTY.
if [[ -n "${NO_COLOR:-}" ]]; then
    USE_COLOR=0
elif [[ -n "${FORCE_COLOR:-}" || "${GITHUB_ACTIONS:-}" == "true" || "${CI:-}" == "true" ]]; then
    USE_COLOR=1
elif [[ -t 1 ]]; then
    USE_COLOR=1
else
    USE_COLOR=0
fi

if [[ $USE_COLOR -eq 1 ]]; then
    C_RED=$'\033[1;31m'           # bold red
    C_ORN=$'\033[1;38;5;208m'     # bold true-orange (256-colour, not the brown-ish ANSI yellow)
    C_DIM=$'\033[2m'              # dim
    C_OFF=$'\033[0m'              # reset
    C_BOLD=$'\033[1m'             # bold
else
    C_RED='' C_ORN='' C_DIM='' C_OFF='' C_BOLD=''
fi

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
        FORMAT_LOG="$(mktemp -t gempba-fmt.XXXXXX.log)"
        printf '%s\0' "${SOURCE_FILES[@]}" \
            | xargs -0 -n 32 -P "$JOBS" "$CLANG_FORMAT" --dry-run --Werror --style=file 2>&1 \
            | tee "$FORMAT_LOG" \
            | sed -E \
                -e "s|^([^[:space:]][^:]*):([0-9]+):([0-9]+):|${C_BOLD}\1:\2:\3:${C_OFF}|" \
                -e "s| warning: | ${C_ORN}warning:${C_OFF} |g" \
                -e "s| error: | ${C_RED}error:${C_OFF} |g" \
                -e "s| (\[-W[a-zA-Z0-9.+/-]+\])$| ${C_DIM}\1${C_OFF}|" || true
        FMT_RC=${PIPESTATUS[1]}

        FORMAT_ISSUES_FILE="$(mktemp -t gempba-fmt-issues.XXXXXX.txt)"
        grep -E ': (warning|error): ' "$FORMAT_LOG" \
            | sed -E "s|^${WORKSPACE}/||" \
            | sort -u > "$FORMAT_ISSUES_FILE" || true
        FORMAT_ISSUE_COUNT=$(wc -l < "$FORMAT_ISSUES_FILE" | tr -d ' ')

        if [[ "$FORMAT_ISSUE_COUNT" -gt 0 ]]; then
            echo
            printf '%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n' "$C_RED" "$C_OFF"
            printf '%s🚨 clang-format violations (%d) — run scripts/lint.sh --fix to auto-format%s\n' "$C_RED" "$FORMAT_ISSUE_COUNT" "$C_OFF"
            printf '%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n' "$C_RED" "$C_OFF"
            echo
            nl -ba -w2 -s'. ' "$FORMAT_ISSUES_FILE" \
                | sed -E \
                    -e "s|^([[:space:]]*[0-9]+\. )([^:]+):([0-9]+):([0-9]+):|\1${C_BOLD}\2:\3:\4:${C_OFF}|" \
                    -e "s| warning: | ${C_ORN}warning:${C_OFF} |g" \
                    -e "s| error: | ${C_RED}error:${C_OFF} |g" \
                    -e "s| (\[-W[a-zA-Z0-9.+/-]+\])$| ${C_DIM}\1${C_OFF}|" \
                | sed 's/^/  /'
            echo
            printf '  Full log: %s%s%s\n' "$C_DIM" "$FORMAT_LOG" "$C_OFF"
            EXIT_STATUS=1
        elif [[ $FMT_RC -ne 0 ]]; then
            printf '%s❌ clang-format exited with status %d%s (full log: %s)\n' "$C_RED" "$FMT_RC" "$C_OFF" "$FORMAT_LOG" >&2
            EXIT_STATUS=1
        else
            echo "✅ clang-format clean"
            rm -f "$FORMAT_LOG"
        fi
        rm -f "$FORMAT_ISSUES_FILE"
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
        TIDY_LOG="$(mktemp -t gempba-tidy.XXXXXX.log)"
        # Stream progress to the user but drop the "NNN warnings generated."
        # noise lines (those are clang-internal counts before -header-filter
        # culls system headers, not real diagnostics). The full raw log stays
        # in TIDY_LOG without ANSI codes for later grepping.
        "$RUN_CLANG_TIDY" "${TIDY_ARGS[@]}" "$FILE_FILTER" 2>&1 \
            | tee "$TIDY_LOG" \
            | grep -vE '^[[:space:]]*[0-9]+ warnings? generated\.' \
            | sed -E \
                -e "s|^([^[:space:]][^:]*):([0-9]+):([0-9]+):|${C_BOLD}\1:\2:\3:${C_OFF}|" \
                -e "s| warning: | ${C_ORN}warning:${C_OFF} |g" \
                -e "s| error: | ${C_RED}error:${C_OFF} |g" \
                -e "s| (\[[a-zA-Z0-9._/-]+\])$| ${C_DIM}\1${C_OFF}|" || true
        TIDY_RC=${PIPESTATUS[0]}

        ISSUES_FILE="$(mktemp -t gempba-tidy-issues.XXXXXX.txt)"
        grep -E ': (warning|error): ' "$TIDY_LOG" \
            | sed -E "s|^${FILTER_ROOT}/||" \
            | sort -u > "$ISSUES_FILE" || true
        ISSUE_COUNT=$(wc -l < "$ISSUES_FILE" | tr -d ' ')

        if [[ "$ISSUE_COUNT" -gt 0 ]]; then
            echo
            printf '%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n' "$C_RED" "$C_OFF"
            printf '%s🚨 clang-tidy diagnostics (%d) — must be fixed%s\n' "$C_RED" "$ISSUE_COUNT" "$C_OFF"
            printf '%s━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━%s\n' "$C_RED" "$C_OFF"
            echo
            # Per-line styling: file:L:C bold, "warning:" red+bold, "error:" red+bold,
            # trailing [check-name] dimmed.
            nl -ba -w2 -s'. ' "$ISSUES_FILE" \
                | sed -E \
                    -e "s|^([[:space:]]*[0-9]+\. )([^:]+):([0-9]+):([0-9]+):|\1${C_BOLD}\2:\3:\4:${C_OFF}|" \
                    -e "s| warning: | ${C_ORN}warning:${C_OFF} |g" \
                    -e "s| error: | ${C_RED}error:${C_OFF} |g" \
                    -e "s| (\[[a-zA-Z0-9._/-]+\])$| ${C_DIM}\1${C_OFF}|" \
                | sed 's/^/  /'
            echo
            printf '  Full log: %s%s%s\n' "$C_DIM" "$TIDY_LOG" "$C_OFF"
            EXIT_STATUS=1
        elif [[ $TIDY_RC -ne 0 ]]; then
            printf '%s❌ clang-tidy exited with status %d%s (full log: %s)\n' "$C_RED" "$TIDY_RC" "$C_OFF" "$TIDY_LOG" >&2
            EXIT_STATUS=1
        else
            echo "✅ clang-tidy clean"
            rm -f "$TIDY_LOG"
        fi
        rm -f "$ISSUES_FILE"
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
