# PCM playback and licensing-review qualification

English · [简体中文](2026-11-13-pcm-and-licensing-review.zh-CN.md)

Date: 2026-11-13. Software source: `a258670d92525c43e5dd6cf104b9965108c9198c`. [Machine evidence](2026-11-13-pcm-and-licensing-proof.json). This is a local review refresh; no tag, public release or official catalog was published. License clearance remains **not cleared**, as detailed in the [assessment](../licensing-assessment.md) and [dependency evidence](2026-11-13-license-inventory.json).

## Result and ownership

Provider WAV playback now uses a finite native PCM source and QAudioSink. The owning research module validates canonical mono/stereo 16-bit WAV, converts device rate/channel/sample type incrementally, and manages completion, stop, restart and errors. The host consumes the same generic ResearchAudio result and exports identical WAV bytes. Reads are bounded at 64 KiB; conversion does not allocate a complete expanded output. This presentation conversion does not alter recovery evidence or I/Q sample codecs.

Qt 6.8 audio initialization selects its built-in audio-only fallback before any device query, then restores the original backend environment for separately launched tools. FM and television use the same initialization. The inspected Qt 6.8.2/6.8.3 implementation supports this path; source-level assumptions are guarded by actual output and loaded-image tests. The Qt fallback diagnostic about only audio-device APIs being available is expected. No private Qt header/API or copied Qt implementation was added.

Windows deployment omits Qt codec plugins and their FFmpeg SDK. macOS assembly removes unused Qt media plugins/foreign FFmpeg ABIs and restores the native television SDK before relocation. A separate fixture checks preservation and repeatability; it is not native Mac acceptance. Linux uses system Qt, so a runtime guard also prevents its installed FFmpeg plugin from being loaded. Each extracted archive is checked for one real runtime per FFmpeg library.

## Verification

| Check | Windows | Receiver-connected Linux workstation |
|---|---|---|
| Full CTest on the stated source | 103 total: 102 passed, one existing FM-device skip, zero failed | 105 total: 102 passed, three existing SDRplay isolation skips, zero failed |
| New finite PCM output | Passed: 48 kHz mono/stereo and 44.1 kHz mono; completion, stop and restart | Passed: same formats and lifecycle |
| Independent conversion truth | PCM header exclusion, partial frames, rate/channel conversion, unsigned/32-bit normalization and unchanged WAV passed | Passed |
| Runtime closure | Extracted-package Qt DLL paths verified; no FFmpeg codec/backend loaded in the finite PCM process | Process mappings sampled; no FFmpeg codec/backend loaded in the finite PCM process |
| Real archives/toolboxes | Application, two receiver modules and example research tool transactions; Chinese/non-BMP extraction; GUI/workspace and desktop checks passed | Passed, including native X11 workspace startup |
| Architecture/resources | Existing dependency and extension checks passed | Passed |

Output qualification uses muted synthetic PCM. It verifies the native device pipeline and finite lifecycle, not human listening or analog output quality. The existing Windows FM real-time test did not qualify its required compatible default output on this setup; the new preview supports preferred-device conversion, and its successful test does not turn that FM skip into a pass. No new sustained RF/capture guarantee is made.

An earlier Windows candidate transaction encountered access denied at a staging-directory rename. The same archive passed a direct rerun and the final fresh archive passed. The cause was not established; no assertions were relaxed and no system protection was disabled.

## Installed review builds

Windows: `C:/Users/vicliu/Apps/Foreshadow-Review-20261113-r2`.
Linux: `/home/vicliu/Applications/Foreshadow-Review-20261113-r2`.
Both launch in English and retain the six review cases. Existing profile/layout data were copied without managed-distribution state; the old r1 directories and processes were preserved. Build-info, module discovery and live-provider checks passed after extraction. The Windows desktop shortcut and Linux application-menu entry point to r2.

Full-stage archive SHA-256:

- Windows: `2e38c18d19b40ad6fa709ee3b38a176d0fdfa91d325acdde842dd501321f4351`
- Linux: `9ee4f296525f6aad6d6a1169275e4abcb3aab8bf179d12f2e22d524b8e193eb0`

The attached machine evidence records package IDs, source identity, component hashes, deployment file counts and raw validation step results. Native macOS build, audio, relocation/signing and deployment for this revision remain unverified. The prior Mac qualification state is not replaced by the bundle-cleanup fixture.

## Licensing boundary

The audit recommends GPL-3.0-or-later for authorized first-party application code, while preserving third-party terms. No top-level LICENSE grant was adopted in this assessment. Complete Qt/receiver/transitive/runtime notices and corresponding-source delivery, exact SDRplay API permissions, optional extractor and firmware licensing, external asset rights and applicable patent questions remain open. Functional acceptance and removing a duplicate codec runtime are not license clearance. Every independent toolbox needs its own applicable notice/source closure through provider declarations and generic packaging, without central provider-ID branches.
