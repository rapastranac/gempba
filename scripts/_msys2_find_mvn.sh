#!/bin/bash
# Side-effect-free helper library: defines a function, does nothing on its own.
# Source from MSYS2 bash; do not execute directly.
#
# MSYS2 with `path-type: minimal` (the default) strips Windows PATH down
# to system dirs, so Windows-installed apps under Program Files (Apache
# Maven, Eclipse Adoptium JDK, …) are NOT visible from the MSYS2 shell.
# cmd's `where` and `echo %VAR%` would inherit the same stripped env and
# miss them too.  PowerShell reads the registry-stored Machine/User env
# directly, which is unaffected — use it to discover mvn when bash
# doesn't already know about it.
#
# Used by:
#   scripts/build_jar_windows.sh
#   .github/workflows/ci-java.yml (build-windows job's test step)
# so the dev-machine and CI paths stay in lockstep.

# Prepends the directory containing mvn.cmd to PATH if mvn is missing.
# No-op when mvn is already on PATH.  Returns 0 on success (mvn now
# resolvable), 1 if no mvn.cmd was found in either Windows scope.
setup_mvn_msys2_path() {
    command -v mvn >/dev/null && return 0
    local win_path entries e unix_e
    win_path=$(powershell.exe -NoProfile -Command \
        "[Environment]::GetEnvironmentVariable('PATH','Machine') + ';' + [Environment]::GetEnvironmentVariable('PATH','User')" \
        2>/dev/null | tr -d '\r')
    IFS=';' read -ra entries <<< "$win_path"
    for e in "${entries[@]}"; do
        [[ -z "$e" ]] && continue
        unix_e=$(cygpath -u "$e" 2>/dev/null) || continue
        if [[ -f "$unix_e/mvn.cmd" ]]; then
            export PATH="$unix_e:$PATH"
            return 0
        fi
    done
    return 1
}
