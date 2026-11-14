# Native television dependencies

English · [简体中文](README.zh-CN.md)

The native television chain and desktop player are implemented. Qualification
is separated into waveform, transport, codec, presentation and platform evidence;
a native decoder test alone does not qualify on-air reception. See the
[television specification](../docs/20-dtmb-television.md) and dated acceptance reports.

| Dependency | Fixed source | License and use |
|---|---|---|
| dtmb-sdr | `ningzichun/dtmb-sdr` at `2cc94c012128d6fa3e8d6de456b3bb57e51990a9` | MIT. Native, private receiver helpers and LDPC tables. No Python runtime. |
| FFmpeg | `FFmpeg/FFmpeg` at `cd4e96eb314630788b7ff2f9820c395788c023e2` | LGPL 2.1 or later for the selected shared-library build; GPL and nonfree components are disabled. Includes native AVS1-P16 Broadcasting decoding. |
| DRA core | `maliwen2015/ffmpeg_cavs_dra` at `eb94ceb887b47b6ecfbda9da43273dd65246df1e` | MIT. Only decoder C sources; provenance and local fixes are recorded in `dra/UPSTREAM.md`. |

The dtmb-sdr source archive SHA-256 is
`87ecbf94ae4873bcc65c3a284ab4e2bfeecf26f2f8135a2f0ff2d98af8338c41`.
The FFmpeg source archive from
<https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/cd4e96eb314630788b7ff2f9820c395788c023e2>
has SHA-256
`e3976ef0636e9c12d68c193abc4898ab50f05be3a4996026abc60c18be7447d4`.

FFmpeg 9.0.2 is insufficient for AVS+ acceptance: its decoder only accepts the
JiZhun profile. Do not replace the pinned source with the system library merely
because a codec named `cavs` is present. The official AVS+ FATE input must match
the official decoded-frame checksums. DRA requires its own real encoded input
and PCM validation; a synthetic codec-provider fixture only validates extension.

Public receiver/reference media and broadcast captures are validation inputs,
not redistributable application assets; keep them outside tracked source and
release packages. Releases must include applicable dependency licenses and
source/relink information, build the same pinned implementations for each
platform, and validate actual installed binaries before acceptance.

Desktop builds now assemble the shared FFmpeg SDK from the fixed source by
default. `SIGNALSCOPE_FFMPEG_ROOT` is an explicit development override;
`SIGNALSCOPE_TELEVISION=OFF` permits builds that intentionally omit television.
Linux/macOS require a shell, GNU Make and NASM on x86; Windows x64 builds use
MSVC and an MSYS2 shell with Make/NASM. The release workflow provides these
tools. Installation includes shared runtimes, licenses, the complete FFmpeg
source archive and its build recipe. Platform and final archive acceptance is
still required; adding a build rule is not evidence that a platform passed.

The Windows build recipe accepts both normal and uppercased MSVC environment
keys. For a compiler installation with only localized diagnostics, it makes a
build-tree copy of the pinned FFmpeg configure script and removes four line
anchors from Microsoft-token detection. Exact upstream contexts are checked;
the downloaded source and packaged source archive remain unchanged. The
packaged `BuildFfmpeg.sh` contains the complete transformation and invocation.
It also makes a build-tree copy of the pinned resource wrapper to pass UTF-8
code page 65001 to rc.exe, so localized compiler metadata remains valid input.
This changes compiler identification, not codec algorithms or feature checks.

`src/adapters/television/dtmb/symbol_sampler.hpp` derives its bounded polyphase
SRRC bank from the pinned dtmb-sdr `core/cpp/src/iq.cpp`. The original MIT notice
is retained in `dtmb-sampler-LICENSE`. Project extensions provide fractional
decision phase, absolute finite-probe origins, an incremental time accumulator,
millihertz clock correction and persistent bounded worker groups. The private
CF32 sampler protocol is version 3; the receiving adapter checks that version
and uses the same measured timing and clock in probe and streaming processes.
Clock acquisition uses superframe correlation, including negative CW/noise
tests. These changes do not establish long-term clock tracking or reception
quality by themselves; the implementation's actual acceptance evidence applies.

Reviewed build-tree corrections to the pinned PN595 sequence and finite FEC
tails are documented in [dtmb-patches/README.md](dtmb-patches/README.md).
The exact patch manifests accompany the packaged dependency notices.
