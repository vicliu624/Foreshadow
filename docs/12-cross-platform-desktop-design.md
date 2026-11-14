# Cross-platform desktop design record

English · [简体中文](12-跨平台桌面与接收机设计.md)

This document records the desktop design and experiments begun on 5 November 2026. It contains proposals, subsequently implemented features and superseded deployment choices. Current normative behavior is in specifications 13–26; current build and operation instructions are in documents 07 and 09. Historical counts below describe their own candidate and environment.

## Product and engineering baseline

The intended desktop supported English and Simplified Chinese on Windows, Linux and macOS, with the uConsole as one Linux host. The C++ acquisition, recording, replay and analysis services were to remain independent of the replaceable UI. Python was optional research tooling, rather than a production runtime requirement.

Build work was batched around a frozen candidate: reuse downloaded dependencies and compiler caches, run the full matrix after the relevant changes stabilize, and rebuild packages when packaging or runtime closure changes. A historical recommendation to clean an old uConsole installation is superseded by the controlled transactions and rollback rules in specification 17.

The old Windows/Debian/uConsole baseline was `baaddb9`; an Apple Silicon macOS run used `8123d5f` and was blocked from the newer OOK test by account access. The associated historical runs were [42310468102](https://github.com/vicliu624/AMC/actions/runs/42310468102) and [42311325887](https://github.com/vicliu624/AMC/actions/runs/42311325887). Those revisions are not the current waveform acceptance candidate.

Qt Widgets and LinguistTools formed the baseline. Minimum Qt was 6.2 outside macOS and 6.8 for the macOS packaging path; SerialPort supported test equipment and Multimedia supported complete audio builds. Noto CJK was a Linux font option. `BUILD_TESTING=OFF` was supported for production-only builds.

Windows used vcpkg RTL-SDR with `static-md` and dynamic SoapySDR. The independent SDRplay module used SoapySDRPlay3 0.5.2 at pinned upstream revision `48bd8b…` and the external SDRplay API 3.15. Runtime discovery used registry `Install_Dir` or `AMC_SDRPLAY_API_DIR` and checked architecture. These are provider/runtime responsibilities rather than UI rules.

## Localization, paths and packages

Windows ZIPs and Linux/macOS tar archives had to preserve their platform behavior and executable permissions. macOS bundles and separately delivered research tools had distinct installation locations. Validation had to run extracted executables from paths containing spaces and Unicode characters; inspecting archive names was insufficient.

Qt's Chinese translation catalog was embedded when required. Saved language preference took precedence over the system preference list; unavailable languages fell back to English. A one-launch language override did not save a new preference. Region-specific numeric display was independent of UI language. Editors accepted localized decimal input and AS CII forms, while raw JSON, sample indices and protocol values remained invariant. Timestamp labels distinguished source time from wall-clock observation time.

Process CPU was normalized against logical processors, RSS was measured separately and peak values could not be below an observed instantaneous value. Unknown measurements were omitted or shown as unknown, rather than invented as zero.

The historical macOS environment used Xcode 26/Apple Clang 15 with minimum macOS 15. Bundle languages included English and Chinese. Qt frameworks and `@rpath` dependencies were closed inside the application and signed locally with ad-hoc signatures. Unneeded plugins with unresolved framework dependencies were excluded. Twenty tests passed on that earlier Apple Silicon environment. This does not replace the later Intel macOS 14 acceptance and toolchain in specification 26.

`--settings-dir` isolated settings from ordinary user profiles. Event output and research working directories were separate. Persistent compatibility names remain stable after the Foreshadow rename.

## Native and emulated platform evidence

An early Debian x86-64 native build used Qt 6.8.2 and GCC 14.2 and passed seventeen checks. Native ARM replay agreed on a 120,000-sample case. VOLK 2.5 and the CI 3.2 version were compared with 24 alternating runs, twelve each. A BW 500/SF7 event retained SFD indices 40,250–41,258; center-frequency difference was approximately 0.000016 Hz and level difference 1.9×10⁻⁶ dB. Median wall times were 134.2 and 133.3 ms, including startup. This was agreement on that recording, not a comprehensive numerical equivalence proof.

Six LoRa edge fixtures included three positives, including weak SFD, low-edge and approximately −7 dB conditions, plus a malformed SFD and two periodic-FSK negatives. Known transmitter parameters were not supplied to the blind detector.

Two actual AIO2 CLI runs of 25 seconds at 478.5 MHz, 1.75 MS/s and 1 MHz search width processed 43.75 million samples each with all five detector switches enabled. No software drops/skips were reported; CPU was approximately 47% and 46%, RSS 70.9 and 71 MiB. The driver statistics did not establish hardware continuity.

The Linux aarch64 cross-toolchain used `/usr/aarch64-linux-gnu` or `AMC_AARCH64_ROOT`, with GUI/Qt/RTL-SDR/SDRplay disabled and KissFFT selected. GNU 13.3 and QEMU 8.2 passed eight core tests; a seven-million-sample FM replay and a Chinese power-report example were also exercised. An emulated Debian ARM GCC14/Qt 6.8.2 desktop installed and displayed both languages. Real-time FSK scanned only 3/53 windows and skipped fifty; replay slowed to 0.1 passed. Emulated correctness therefore did not qualify throughput.

A uConsole Wayland run processed 26,542,080 samples in 15.01 seconds with no analysis drops, at 2.17 CPU cores. The UI accounted for approximately 0.224 cores/10.3%, RSS128.7 MiB and PSS107.3 MiB. The environment contained background work and did not isolate pure Qt cost. Four spectrum updates per second and a common-input UI benchmark were design targets, not measurements of every frontend.

## Session, replay and snapshot design

The common session layer was responsible for acquisition/replay convergence, ownership, shutdown and partial failures. Typed status was preferred over reinterpreting command output or testing device IDs. Native replay needed pacing, pause/cancel and visible lifecycle states. `replay-events --speed 0` meant unpaced processing; speed1 meant real-time pacing. This differed from offline batch scanning.

Snapshot work used bounded quarter-second requests, pre-trigger retention, explicit gaps and busy skips. UI threads submitted requests and polled results rather than running DSP directly. Read-only borrowed I/Q could have multiple listeners; acquisition discontinuity and analysis rejection were distinct, and listener exceptions were isolated.

The proposed module graph distinguished detection, channelization/demodulation, decoding/parsing, display/output and generic mathematical tools. Units, clocks, resampling, delayed feedback, bounded backpressure, memory budgets and reproducibility were explicit design concerns. A general live, multi-port graph and continuous multidisciplinary control system were proposals, not delivered merely because individual snapshot tools existed.

## Desktop research and audio behavior

Event context menus opened the associated recording with approximately 20 ms padding and a two-million-sample overview limit. Shift+F10 provided keyboard access. A stale event could not silently select a different file. LoRa structural views and roughly five-second FM previews retained source association. The common I/Q overview had six native views; visual decimation did not change analysis samples.

Narrow-FM evidence suppressed overlapping FSK interpretations where appropriate. The old rule that stopping monitoring stopped FM is superseded by independent receiver leases in specification 21. The early live audio path used mono48 kHz PCM, 50 µs deemphasis and four bounded blocks/250ms. Recorded export allowed up to30s, PCM 16, 50/75 µs or disabled deemphasis and 50/75 kHz deviation, default75 kHz, through `fm-audio`.

Native detection menus were assembled from descriptors, including CSS, OFDM, PSK and 4-FSK. The saved `four-fsk` alias was frozen compatibility metadata. A narrowband PSK candidate could be evaluated before 4-FSK; wideband-only analyses should not acquire arbitrary narrowband identity branches. Manual LoRa regions had explicit parameters and a64 MiB bound; complete structural results and plot intervals were retained. Legacy Python diagnostics remained research tools.

Native scan windows were quarter-second with50% overlap and absolute indices. Multiple legitimate observations of the same transmission were not counts of independent devices. The early OFDM CP candidate did not yet estimate carrier offset; later reception is documented in specification 25.

## Historical external-tool protocol

The early local-manifest/executable installation design is superseded by specification 17. Its snapshot protocol remains relevant to compatible tools:

| Field or behavior | Contract |
|---|---|
| API 1 identity | Dotted `id`; nonempty English title; localized fallback; title at most80 UTF-16 code units |
| Invocation | Adjacent executable; absolute `--input` SigMF and `--output` paths; `--language` |
| PNG output | At most32 MiB and4096 pixels per dimension |
| Text output | Nonempty UTF-8, at most1 MiB |
| WAV output | At most32 MiB; PCM 16 little-endian; 44-byte basic header; 1/2channels; 8–192 kHz |
| Execution | Default30s timeout, cancellation, explicit trust and read-only source access |
| Sample range | `sampleRange: true` adds `--first-sample`/`--samples`; seconds round half up; blank uses defaults; switching units clears incompatible values |
| Binary output | Declared format such as `vendor.telemetry-frames`, version 1; at most32 MiB; metadata and byte-preserving export |
| API 2 chaining | Match declared input format; temporary byte-preserving intermediate input; cancellation retains the previous result; Return input goes back one step; opening a recording clears history |

The native power-report example used five seconds of input. Snapshot chaining did not establish a generic real-time graph, native live panels or multi-port stream capability.

## Receiver configuration and continuous-input experiments

SDRplay used native complex signed16-bit pairs, SoapySDR0.8 and the external API service/ABI. The original2 MS/s defaults, AntA/B choices, AntC restriction below200 MHz,1 kHz–2 GHz tuning notes,2/2.048/3 MS/s presets and1.536 MHz IF observations were device-specific historical assumptions. They are superseded by provider-owned capabilities, whole-configuration validation and actual readback in specifications13–15. Serial identity and historical index aliases must remain stable. IF gain/attenuation belongs to the provider.

Overflow reporting distinguished `stream_discontinuity`, read counts, short reads, timeouts, min/max block size, merged blocks and startup-settling samples from delivered data. The following failures were retained rather than converted into lossless claims:

| Run/change | Observed result |
|---|---|
| Emulated ARM real-time FSK |3/53 windows processed; 50 skipped; slowed0.1 replay passed |
| Native uConsole FSK and seven-million-sample FM replay |Real-time FSK passed; FM 214 windows,0 skips,1event |
| Paced fake SDRplay,15s,2 MS/s,4096-sample reads |30million samples; old queue skipped112/105 windows, revised queue5/6; main recording retained input |
| Merge into64 KiB CS16 blocks/16,384complex samples |1832windows,0 skips, queues of2/8 blocks |
|475.125 MHz,7.5ms FSK in120,000 samples |No event before merging; one afterward |
| Chinese offscreen,15s |29,974,528recorded samples; 1830 blocks,0FSK skips, but8192 samples pending at close (4.1ms) |
| Shutdown correction rerun |27,656,192 samples fully accounted for, without that pending tail |
|30s,2.048 MS/s, LoRa disabled |61.44million samples; no recorded skips |
|30s,3 MS/s |90million input; main pipeline rejected20,709,376 samples; narrow stage skipped5476windows |
|15s,2 MS/s all detectors, or3 MS/s narrowband only |No reported software drops for those runs |
|3 MS/s with LoRa / all detectors |7,382,336 /7,316,800 samples rejected |
|Four LoRa workers /128 KiB blocks |Still5,783,552 /6,071,616 samples rejected |

The writer consumed the complete accepted input in those comparisons. The uConsole bottleneck was the LoRa analysis path, rather than evidence that USB was lossless or universally at fault. Later scheduling changes require their own sustained-input evidence.

## Frontend alternatives and references

Qt Widgets was the deliverable baseline. FLTK was a potentially lighter option; wxWidgets, Qt Quick/GPU, Dear ImGui and a Tauri/WebView frontend had different deployment, interaction and process-accounting costs. A fair comparison must use the same receiver input, display cadence, analysis and whole-process accounting. Framework reputation alone cannot establish performance.

Design references: [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html), [Qt Linguist](https://doc.qt.io/qt-6/qtlinguist-index.html), [FLTK manual](https://www.fltk.org/doc-1.3/), [wxWidgets](https://www.wxwidgets.org/), [Dear ImGui](https://github.com/ocornut/imgui), [Tauri](https://tauri.app/) and [SDRplay API](https://www.sdrplay.com/api/). These references describe their projects; the measurements above describe this repository's historical candidates.
