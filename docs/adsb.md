# ADS-B module

English · [简体中文](adsb.zh-CN.md)

## uConsole local deployment (2026-11-24)

At the owner's request, the tested 0.2.12 build was installed persistently at
`~/.local/share/SignalScope/local-builds/adsb-20261124`. The desktop/application
entry is **Foreshadow ADS-B**, launched by `~/.local/bin/foreshadow-adsb` with a
separate settings directory inside that installation. Native startup and the
actual process executable path were verified. Open View → ADS-B air traffic.

This is a parallel local build, not a signed public release. The existing AMS
0.2.11 launchers and configuration remain unchanged and retain DTMB television.
This ADS-B build contains FM and the waveform laboratory but does not contain
the television panel; its optional full-TV rebuild was stopped when dependency
download remained unavailable. Existing recordings were not moved or removed.

## Status

Implemented and tested on ARM64 Linux/uConsole, 2026-11-24. This is source-level
delivery, not a published release or physical RF acceptance claim.

ADS-B is a bundled module updated with Foreshadow, not an independently loadable
binary toolbox. It currently targets 1090 MHz extended squitter (DF17/DF18),
not 978 MHz UAT or general Mode S replies.

## Ownership and integration

`plugins/adsb/core` owns envelope PPM acquisition, CRC-24, message fields,
airborne global CPR and bounded aircraft history. It depends only on the C++
standard library. The detection adapter uses `SnapshotExtension`; the offline
adapter uses `RecoveryProvider`; the desktop adapter uses `LiveResearchProvider`
and `ReceptionAcquire`. Registration stays at the corresponding assembly points.
The map consumes aircraft snapshots without receiver or decoding rules.

The core is compiled into the existing DSP library. Generic scheduling,
receiver sessions, recovery views and workspace management do not route by ADS-B
IDs. I/Q conversion uses the shared sample codec. Module text and offline map
assets install under the `adsb` resource domain.

## Source behavior

Enable ADS-B in detector selection for periodic monitoring. Its 50 ms observation
every second is intentionally intermittent; frames outside the window are not
observed. A complete 1090 MHz ±1 MHz band must fit in the receiver passband.
The current input-rate range is 2–20 MS/s. Configuration coverage is not a
guarantee of decoding under noise, interference or clipping.

The desktop ADS-B panel requests its own reception lease. Receive starts a
1090 MHz demand and Stop releases it. Other active tools can make that demand
incompatible. View/workspace registration remains generic. The offline recovery
entry is `adsb-ppm-crc24` and accepts no advanced parameters.

Only complete CRC-valid DF17/DF18 frames reach semantic parsing. No bit correction
is attempted. Available fields include callsign, Q-coded barometric altitude,
ground-vector velocity, vertical rate and raw airborne CPR. Q=0 altitude and
unimplemented type fields remain unavailable. CRC validates parity, not identity.
DF18 control is retained because addresses need not be ICAO aircraft identities.
DF18 CF3/4/7 remain raw CRC-valid evidence without aircraft-field interpretation
or tracker insertion; their layouts are not decoded by this module.

Airborne positions require matching even/odd frames within 10 seconds on the
same continuous source clock. Gaps reset pairing. Position validity expires after
15 source seconds; targets expire after 120. History is bounded to 1024 targets
and 128 trail points per target. The live input queue budgets actual payload at
8 MiB; processed/rejected input and discontinuities are separate diagnostics.

The map uses bundled Natural Earth 1:50m land contours with Mercator projection.
It supports pan, zoom and locating a positioned target. This is a coarse offline
geographic overview, not a detailed navigation chart. Source URL, original hash
and conversion are retained in `plugins/adsb/map-source.json`. Natural Earth data
is [public domain](https://www.naturalearthdata.com/about/terms-of-use/).

## Evidence and remaining acceptance

Verified on the ARM64 uConsole: standalone GCC compilation, known callsign and
altitude frames, CRC corruption rejection, airborne CPR reference coordinates,
all frame split positions at 2/2.4/4 MS/s, gap rejection, background-before-frame
scanning and tracker expiry. Detector, recovery and desktop sources have passed
syntax compilation; this does not prove integrated runtime behavior.

The public recovery registry/`recover_signal` integration also passed on the
uConsole: a frame split across continuous segments retains its exact byte
support at source origin 2^50; an explicit discontinuity rejects that frame.
The standalone 640×400 map rendered with its installed offline resource; visual
inspection led to adaptive coordinate grid labels. That initial rendering check
was followed by the full-panel and main-application checks recorded below.

The standalone full panel also passed reference I/Q reception through an injected
reception provider, populated one target and stopped successfully. Its 640×480
offscreen screenshot was inspected on the uConsole build with 1:50m coastlines.
This is not yet actual desktop workspace or physical RF acceptance.

The fixed-source CMake desktop/provider build passed all eight selected CTests:
ADS-B detection, codec, live input, recovery, panel, map, extension architecture
and existing snapshot observation. Local/remote hashes matched for 29 ADS-B
source/resource/test files. Full-application and broader regressions are separate.

The final main-application build and all 14 selected CTests passed, including
five-phase timing, recovery budgets, Chinese/fallback, architecture and the
existing recovery/coherent/live-research/independent-extension regressions.
View automatically exposes ADS-B; native desktop restart retained its visibility
and selected tab. See the [workspace screenshot](reports/assets/2026-11-24-adsb-workspace.png).

The paced live-input probe processed all 60,000,000 pairs over 30 seconds at
2 MS/s, recovered 3000/3000 frames, rejected zero pairs and reported zero gaps;
queue high water was 40,000 bytes. This is bounded synthetic software evidence.

At this development checkpoint, unverified qualification included physical RF decoding, long-term hardware/sample-loss
behavior, formal release archives and Windows/macOS runtime/package behavior.
These are not implied by the Linux source build or copied-binary path checks.
At that checkpoint, repository-reviewed material profiles covered Windows
x86_64 and Debian x86_64, not `linux-debian13-arm64`. The uConsole's OpenSSL
package is `3.5.7-1~deb13u3+rpt1`, differing from the reviewed x86_64 source
revision. Relabelling that profile is not package qualification.
The complete `SignalScope` CMake install component subsequently succeeded,
including the application, receiver modules, existing private helpers, ADS-B
catalogs and map assets. The installed application passed provider creation.
The actual `package_release.py` attempt stopped at missing
`share/foreshadow/compliance/inventory.json`; no qualified archive was produced.
At that checkpoint, final distribution acceptance was blocked on corresponding reviewed
platform/dependency materials. Do not substitute a copied executable directory
or an unsigned ad-hoc archive for that gate.

The [published 0.2.13 release](releases/0.2.13.md) uses a distinct
`linux-debian13-arm64` profile for the generic Debian 13 hosted SDK, including
OpenSSL 3.5.9 source materials and an installed-SDK source-identity gate. The
uConsole's Raspberry Pi OpenSSL 3.5.7 runtime remains a separate compatibility
target. The actual hosted artifact passed uConsole startup, English provider,
I/Q replay, AVS+/DRA, toolbox installation and injected receiver-service checks.
Its local development build was not relabelled as the hosted artifact. Final archive
and device qualification are recorded with the release assets; macOS is outside
this release's acceptance scope. Physical RF decoding remains unverified.

Build `adsb_stream_probe` explicitly and run it to measure 30 seconds of 2 MS/s
input with 100 known frames per source second. It reports elapsed time, frame
count and real-time factor. It is not part of timing-sensitive daily CTest and
does not exercise acquisition hardware or the live queue.

On 2026-11-24 the ARM64 uConsole probe recovered 3000/3000 frames from 30 seconds
at 2 MS/s. Baseline elapsed time was 24.7978 s; necessary-condition preamble
rejection measured 2.73899 s (10.9529× source-time throughput). Builds were running
concurrently; these are bounded synthetic-core observations, not isolated CPU
benchmarks, arbitrary-waveform throughput or hardware loss measurements.

Physical capture attempt on the same uConsole: AIO_V2 Ext / RTL2832U, R820T,
1090 MHz, requested 2 MS/s, gain 40.2 dB, 20,000,000 sample pairs (40,000,000 CU8
bytes). The tuner logged `PLL not locked!`; the recording probe decoded zero
CRC-valid frames. Physical reception is therefore unverified. This attempt does
not distinguish missing RF coverage/antenna/tuning from demodulator sensitivity,
and supplies no hardware-loss counter evidence.

An intermediate integrated build failed live-input, recovery-registration and
map tests after sources changed during compilation. A fixed-source rebuild of
the module, registry and test objects subsequently passed all eight selected
contracts, including all three previously failing cases. Do not modify shared
headers while a validation build is running.

Additional standalone checks passed: five fractional sampling phases at
2/2.4/4 MS/s, together with the earlier noise, amplitude, CRC and split/gap
regressions; and native desktop panel startup/resource discovery/screenshot
export from a directory containing Chinese and a non-BMP emoji. These are not
formal release-package or main-application path acceptance. Five-phase search
throughput must be measured separately from the earlier single-phase probe.
The five-phase version subsequently recovered 3000/3000 frames in 15.2001 s
for the same 30-second input (1.97367×). This supersedes the single-phase
throughput for the current synchronization algorithm, under the same limited
synthetic-core scope.

Chinese panel labels, table headings and counters were inspected in a native
uConsole desktop screenshot. The panel reference-reception test passed both
`zh-CN` and absent `fr-CA` (English fallback), using the frozen desktop module
build. Later synchronization/counting changes still require the final combined
rerun; this language evidence does not establish those changes.
