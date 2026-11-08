#!/bin/bash
# Check that a JNI library depends only on the OS and, for mp-mpi, on MPI, so it loads on a machine with nothing
# else installed.
#
# Usage:
#   bash scripts/check_jni_deps.sh <gempba_jni.so|.dll|.dylib> mt|mp-mpi
#
# Linux needs readelf and objdump, Windows needs objdump from MSYS2, and macOS needs otool.

set -euo pipefail

usage() {
    echo "Usage: $(basename "$0") <native library> mt|mp-mpi" >&2
    exit 2
}

[[ $# -eq 2 ]] || usage
lib=$1
flavor=$2
[[ -f "$lib" ]] || { echo "$lib does not exist." >&2; exit 2; }
case "$flavor" in
    mt|mp-mpi) ;;
    *) usage ;;
esac

# Newest glibc symbol version a Linux library may need: AlmaLinux 8's, which RHEL 8 shares.
glibc_floor=2.28

failed=0
reject() {
    echo "  not allowed: $1" >&2
    failed=1
}

case "$lib" in
    *.so)
        allowed='^(libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux-x86-64\.so\.2|ld-linux-aarch64\.so\.1)$'
        mpi='^libmpi\.so\.[0-9]+$'
        deps=$(readelf -d "$lib" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
        glibc=$(objdump -T "$lib" | grep -o 'GLIBC_[0-9][0-9.]*' | sed 's/GLIBC_//' | sort -t. -k1,1n -k2,2n -u | tail -1)
        echo "$lib needs glibc $glibc"
        if [[ "$(printf '%s\n%s\n' "$glibc" "$glibc_floor" | sort -t. -k1,1n -k2,2n | tail -1)" != "$glibc_floor" ]]; then
            reject "glibc $glibc, newer than $glibc_floor"
        fi
        ;;
    *.dll)
        allowed='^(api-ms-win-.*|advapi32|bcrypt|crypt32|dbghelp|gdi32|iphlpapi|kernel32|ntdll|ole32|oleaut32|psapi|secur32|shell32|shlwapi|ucrtbase|user32|userenv|version|winmm|ws2_32)(\.dll)?$'
        mpi='^msmpi(\.dll)?$'
        deps=$(objdump -p "$lib" | sed -n 's/.*DLL Name: //p' | tr 'A-Z' 'a-z')
        ;;
    *.dylib)
        allowed='^(/usr/lib/|/System/Library/)'
        mpi='/libmpi\.[0-9]+\.dylib$'
        deps=$(otool -L "$lib" | tail -n +2 | awk '{print $1}' | grep -v "$(basename "$lib")$" || true)
        echo "$lib targets macOS $(otool -l "$lib" | awk '/minos/ {print $2; exit}')"
        ;;
    *)
        echo "$lib is not a .so, .dll or .dylib." >&2
        exit 2
        ;;
esac

echo "$lib depends on:"
while read -r dep; do
    [[ -n "$dep" ]] || continue
    echo "  $dep"
    if [[ "$dep" =~ $allowed ]]; then
        continue
    fi
    if [[ "$flavor" == mp-mpi && "$dep" =~ $mpi ]]; then
        continue
    fi
    reject "$dep"
done <<< "$deps"

if [[ "$flavor" == mp-mpi ]] && ! grep -Eq "$mpi" <<< "$deps"; then
    reject "no MPI library, though $flavor needs one"
fi

if (( failed )); then
    echo "$lib depends on more than the OS and MPI." >&2
    exit 1
fi
echo "$lib depends only on the OS$([[ "$flavor" == mp-mpi ]] && echo ' and MPI')."
