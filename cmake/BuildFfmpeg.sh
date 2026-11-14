#!/usr/bin/env bash
set -euo pipefail
source_dir=$1
build_dir=$2
prefix=$3
jobs=$4
shift 4
if [[ ${OS:-} == Windows_NT ]]; then
    source_dir=$(cygpath -u "$source_dir")
    build_dir=$(cygpath -u "$build_dir")
    prefix=$(cygpath -u "$prefix")
    # MSYS also has a link executable. The actual native linker must win.
    # Direct bash invocation does not load /etc/profile. Inherited Windows
    # PATH can omit this MSYS installation's tools or resolve another shell's
    # programs. Keep MSVC link.exe first, then this bash's own tool directory.
    # Windows environment names are case-insensitive, while MSYS names are
    # case-sensitive. Launchers such as Python can uppercase the inherited key.
    vc_tools=${VCToolsInstallDir:-${VCTOOLSINSTALLDIR:-}}
    if [[ -z $vc_tools ]]; then
        echo "The native media build requires the MSVC tools environment" >&2
        exit 1
    fi
    export PATH="$(cygpath -u "$vc_tools")/bin/Hostx64/x64:/usr/bin:$PATH"
    command -v nasm
    nasm -v
fi
mkdir -p "$build_dir" "$prefix"
cd "$build_dir"
command -v "${MAKE:-make}" >/dev/null
configure_command=("$source_dir/configure")
if [[ ${OS:-} == Windows_NT ]]; then
    # A Chinese-only MSVC installation can ignore the VSLANG hint and put
    # translated words before the invariant Microsoft token. Relax only that
    # line anchor in a build-tree copy of the pinned configure script.
    # The pinned version checks the compiler twice and the librarian once.
    if [[ $(grep -Fc 'grep -q ^Microsoft' "$source_dir/configure") != 3 ]] ||
       [[ $(grep -Fc 'grep ^Microsoft' "$source_dir/configure") != 1 ]]; then
        echo "Pinned FFmpeg MSVC detection context changed; review the build patch" >&2
        exit 1
    fi
    sed 's/grep -q \^Microsoft/grep -q Microsoft/g; s/grep \^Microsoft/grep Microsoft/g' \
        "$source_dir/configure" > "$build_dir/amc-configure-msvc"
    # Preserve configure's original $0 for source-directory discovery without
    # modifying the fetched tree or embedding a path in generated shell code.
    configure_command=(bash --noprofile --norc -c 'script=$1; shift; . "$script"' \
        "$source_dir/configure" "$build_dir/amc-configure-msvc")
    # configure writes the localized compiler identity as UTF-8. rc.exe uses
    # the machine code page by default, which can misparse that generated C
    # string. Keep resources and metadata, and select their actual encoding.
    if [[ $(grep -Fc 'EXTRA_OPTS="-nologo"' "$source_dir/compat/windows/mswindres") != 1 ]]; then
        echo "Pinned FFmpeg resource wrapper context changed; review the build patch" >&2
        exit 1
    fi
    sed 's/EXTRA_OPTS="-nologo"/EXTRA_OPTS="-nologo -c65001"/' \
        "$source_dir/compat/windows/mswindres" > "$build_dir/amc-mswindres"
    chmod +x "$build_dir/amc-mswindres"
    configure_command+=("--windres=$build_dir/amc-mswindres")
fi
"${configure_command[@]}" --prefix="$prefix" --disable-autodetect --disable-everything \
    --disable-gpl --disable-nonfree --enable-shared --disable-static \
    --disable-programs --disable-doc --disable-debug --disable-network \
    --disable-avdevice --disable-avfilter --enable-avcodec --enable-avformat \
    --enable-avutil --enable-swscale --enable-swresample \
    --enable-demuxer=mpegts --enable-protocol=pipe,file \
    --enable-parser=aac,aac_latm,ac3,cavs,h264,hevc,mpegvideo,mpegaudio \
    --enable-decoder=cavs,h264,hevc,mpeg2video,mpeg4,aac,aac_latm,ac3,eac3,mp2,mp3,pcm_s16le "$@"
"${MAKE:-make}" -j"$jobs"
"${MAKE:-make}" install
