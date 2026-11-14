#!/bin/bash
set -euo pipefail

bundle=$1
shift
contents=$bundle/Contents

# Qt can copy optional plugins even when their frameworks are absent.
prune_optional_plugin() {
    local plugin=$1 framework
    shift
    for framework in "$@"; do
        if [[ ! -f "$contents/Frameworks/$framework.framework/Versions/A/$framework" ]]; then
            rm -f "$contents/PlugIns/$plugin"
            return
        fi
    done
}

prune_optional_plugin imageformats/libqpdf.dylib QtPdf
prune_optional_plugin iconengines/libqsvgicon.dylib QtSvg
prune_optional_plugin platforminputcontexts/libqtvirtualkeyboardplugin.dylib QtVirtualKeyboardQml QtVirtualKeyboard

/bin/bash "$(dirname "$0")/RelocateMacRuntime.sh" "$contents" "$contents/Frameworks" "$@"

# install_name_tool invalidates existing signatures on the modified binaries.
codesign --force --deep --sign - "$bundle"
codesign --verify --deep --strict "$bundle"
