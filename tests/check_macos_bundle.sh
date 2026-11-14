#!/bin/bash
set -euo pipefail

unpacked=$1
plist="$unpacked/Foreshadow.app/Contents/Info.plist"
plutil -lint "$plist"
test "$(plutil -extract CFBundleDevelopmentRegion raw "$plist")" = en
test "$(plutil -extract CFBundleLocalizations.0 raw "$plist")" = en
test "$(plutil -extract CFBundleLocalizations.1 raw "$plist")" = zh-Hans
# Check every bundled Mach-O, including Qt plugins that replay may not load.
contents="$unpacked/Foreshadow.app/Contents"
frameworks="$contents/Frameworks"
minimum=$(plutil -extract LSMinimumSystemVersion raw "$plist" 2>/dev/null || true)
while IFS= read -r -d '' binary; do
  kind=$(file -b "$binary")
  [[ "$kind" == *Mach-O* ]] || continue
  dependencies=$(otool -l "$binary" | awk -v binary="$binary" -v minimum="$minimum" '
    function newer(actual, declared, a, d, i) {
      split(actual, a, ".")
      split(declared, d, ".")
      for (i = 1; i <= 3; i++)
        if (a[i]+0 != d[i]+0) return a[i]+0 > d[i]+0
      return 0
    }
    $1 == "cmd" { command = $2 }
    minimum != "" &&
    (($1 == "minos" && command == "LC_BUILD_VERSION") ||
     ($1 == "version" && command == "LC_VERSION_MIN_MACOSX")) {
      if (newer($2, minimum)) {
        print binary ": requires macOS " $2 ", above the bundle minimum " minimum > "/dev/stderr"
        failed = 1
      }
    }
    ($1 == "name" && command ~ /^LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB$/) ||
    ($1 == "path" && command == "LC_RPATH") {
      value = $0
      sub(/^[[:space:]]*(name|path)[[:space:]]+/, "", value)
      sub(/[[:space:]]+\(offset [0-9]+\)$/, "", value)
      relative = value ~ /^@(rpath|loader_path|executable_path)\//
      # LC_RPATH denotes a directory; the loader/executable directory itself
      # is valid without a trailing slash or filename. LC_LOAD_* still names
      # a file, and keeps the stricter relative-path requirement.
      if (command == "LC_RPATH" && value ~ /^@(loader_path|executable_path)$/) relative = 1
      if (!relative &&
          value !~ /^\/(usr\/lib|System\/Library)\//) {
        print binary ": " command " uses external path " value > "/dev/stderr"
        failed = 1
      }
      if ($1 == "name" && value ~ /^@(rpath|loader_path|executable_path)\//) print value
    }
    END { exit failed }
  ')
  while IFS= read -r dependency; do
    case "$dependency" in
      @rpath/*) target="$frameworks/${dependency#@rpath/}" ;;
      @loader_path/*) target="${binary%/*}/${dependency#@loader_path/}" ;;
      @executable_path/*) target="$contents/MacOS/${dependency#@executable_path/}" ;;
      *) continue ;;
    esac
    [[ -f "$target" ]] || {
      printf '%s: missing bundled dependency %s\n' "$binary" "$dependency" >&2
      exit 1
    }
  done <<< "$dependencies"
done < <(find "$unpacked" -type f -print0)
