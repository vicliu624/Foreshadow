#!/bin/bash
set -euo pipefail

root=$1
runtime=$2
shift 2
sign_leaves=false
if [[ "${1:-}" == --sign-leaves ]]; then
    sign_leaves=true
    shift
fi

# Every relocated reference is relative to its own loader, including modules
# installed independently of the application's executable and Frameworks.
relative_directory() {
    local from=$1 to=$2 prefix=""
    while [[ "$to/" != "$from/"* ]]; do
        from=${from%/*}
        prefix="../$prefix"
    done
    if [[ "$from" == "$to" ]]; then
        printf '%s.' "$prefix"
    else
        printf '%s%s' "$prefix" "${to#"$from/"}"
    fi
}

for name in "$@"; do
    [[ -f "$runtime/$name" ]] || { printf 'Missing declared runtime: %s\n' "$runtime/$name" >&2; exit 1; }
    install_name_tool -id "@rpath/$name" "$runtime/$name"
done

while IFS= read -r -d '' binary; do
    [[ $(file -b "$binary") == *Mach-O* ]] || continue
    for name in "$@"; do
        # Leave OS libraries used by Qt and other dependencies on the OS.
        while IFS= read -r dependency; do
            [[ "${dependency##*/}" == "$name" ]] || continue
            [[ "$dependency" != /usr/lib/* && "$dependency" != /System/Library/* ]] || continue
            relative=$(relative_directory "${binary%/*}" "$runtime")
            install_name_tool -change "$dependency" "@loader_path/$relative/$name" "$binary"
        done < <(otool -l "$binary" | awk '
            $1 == "cmd" { command = $2 }
            $1 == "name" && command ~ /^LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB$/ {
                sub(/^[[:space:]]*name[[:space:]]+/, "")
                sub(/[[:space:]]+\(offset [0-9]+\)$/, "")
                print
            }
        ')
    done
    paths=$(otool -l "$binary" | awk '
        $1 == "cmd" { command = $2 }
        $1 == "path" && command == "LC_RPATH" {
            sub(/^[[:space:]]*path[[:space:]]+/, "")
            sub(/[[:space:]]+\(offset [0-9]+\)$/, "")
            if ($0 ~ /^\// && $0 !~ /^\/(usr\/lib|System\/Library)\//) print
        }
    ' | sort -u)
    if [[ -n "$paths" ]]; then
        while IFS= read -r path; do
            install_name_tool -delete_rpath "$path" "$binary"
        done <<< "$paths"
    fi
done < <(find "$root" -type f -print0)

# A bundle is signed by its assembly owner after all nested code is ready.
# Standalone processes have no bundle to own their sibling runtime signatures.
if $sign_leaves; then
    while IFS= read -r -d '' binary; do
        [[ $(file -b "$binary") == *Mach-O* ]] || continue
        codesign --force --sign - "$binary"
    done < <(find "$root" -type f -print0)
fi
