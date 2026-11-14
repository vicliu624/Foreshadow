# FM playback recovery and module controls acceptance

English · [简体中文](2026-11-10-FM播放恢复与模块控制验收.md)

Historical formal0.2.8release, source `5e4f1537b93a1a3defa2cf03f15d84428d2f6ffc`. Six-platform validation, signed distribution,31.42helperactivation and sustained formal-binary checks completed. [Release](https://github.com/vicliu624/AMC/releases/tag/v0.2.8), [release CI](https://github.com/vicliu624/AMC/actions/runs/42839219740), [self-hosted CI](https://github.com/vicliu624/AMC/actions/runs/42839215843).

## Reproduction and ownership

The user heard a broadcast FM event playback stop with an error and lacked independent volume/squelch/device/start/stop controls. On actual31.42audio output, muted-volume0replay of10 MS/sI/Q reproduced `stream_failed=false`, `QAudio::UnderrunError=3`, `QAudio::IdleState=3`. Demodulation had not failed; output misclassified temporary underrun as fatal. Nonzero source sample origins were already supported and were not the cause.

New `LiveResearchProvider`v1 defines a compiled C++live lifecycle, assembled by `live_registry.cpp` and hosted by `LiveTools`. The FM provider owns controls/validation/preferences/resources and its audio implementation; FM modulation/codecmodules own demodulation/actual-byte ingress/continuity; displayproviders own pixel aggregation. Host/window/workspace consume generic components, actions and diagnostics. Existing external snapshot IPC does not automatically provide liveI/Q, nativeQWidgetsorcontinuous playback. Built-in FM ships with the application rather than as a replaceableABItoolbox.

## Workflow and implementation

At this release date, monitoring starts first, then the FM event action opens its panel at the event frequency. View can open it manually. Full-channel-outside-input, absent output or conflicting parameters disable Start with a reason; Center restores the current receive center. Later independent reception leases in specification 21supersede this acquisition lifecycle.

The panel exposes start/stop/apply/retune, output selection/refresh, application volume/mute, squelch/threshold, deemphasis/deviation normalization/recommended values, channel level/SNR estimate/PCM level/latency and separate input-loss/gap/output-underrun/PCM-loss counts. Actual errors persist with code/detail. Hiding continues audio; stopping audio retains reception; stopping reception then stopped audio. Valid saved layouts restore normally and new panels register inView.

SNR uses filtered second/fourth-moment constant-envelope estimation, smoothed over50ms. Interference/fading/clipping affect it; it is not calibrated. Squelch changes do not improve RF SNR. Output is48 kHz mono; stereo pilot evidence does not imply stereo/RDS decoding.

Ingress is an actual-payload-budgetedSPSCbyte ring with bounded discontinuity metadata. Short reads do not shrink buffering time. DSPreads/copies do not hold acquisition enqueue locks. A candidate with approximately1.2%lock-contention rejection failed sustained testing and was not released. Audio device/feed timer moved to a separate output thread; busyUIcannot halt PCM feed and status copies only a small snapshot. Underrun re-prebuffers/recoveries remain distinct from input rejection, latency discard and demodulation failure. Spectrum rendering aggregates each visible pixel's peak/minimum, without changing FFT/input/bandwidth.

## Verification and sustained formal binary

Local49 checks passed. Independent live providers registered, consumed borrowedI/Q, added unknown-eventactions/Viewpanels, stopped/published diagnostics and proved fault/generation isolation without host edits. Dutch-only FM resources changed titles and missing keys fell back toEnglish, includingChinese/non-BMPresource paths.

Wideband FM truth covered2.56/10 MS/s, CU8/CI16, large origins/short reads and PCM equality with whole-input processing. Noise controls checked SNR estimate monotonicity/error bounds. Thousands of tiny callbacks within bytebudget produced no rejects/gaps and matched offline PCM. Ring wrap/gap reset passed. Actual device tests covered300mssourcepause recovery, continuedaudio during300ms GUI eventpause, volume/mute/squelch/reopen, atvolume0without system-volume/default-devicechanges. Native31.42suite had49 checks/no failures/3 SDRplay environment skips. Actual Unicode packages loaded application/receiver/research and completed four real toolbox transactions.

Formal installed/hash-verified GUI ran approximately209 seconds on Debian 13/RSPdx-R2,100 MHz/10 MS/s/CI16/10 MHz/all detectors/default16 MiB main+auxqueues; FM 97 MHz/defaultIEC958digital output:

| Metric | Result |
|---|---:|
| Receiver/recording/main/auxiliary pairs, each |2,076,934,144|
| Main rejection/gaps, auxiliary rejection |0/0,0|
| FM input/processed pairs |2,075,869,184/2,075,869,184|
| FM input rejection/gaps/PCM discard |0/0/0|
| Output underruns |0, playing before stop|
| FM queue peak/budget |851,968/8,388,608 bytes|
| Driver short reads/timeouts |32,210/0|

FM started after source context became available, so its interval differs. Maincounts drained after stop; FM statuswas sampled before intentional stop. An earlier210secondnativecandidate had4recovered underruns, no input/PCM loss; it remains evidence. Neither run guarantees arbitrary-load continuity. No hardware sequence existed; 10 MS/s/8 MHzIFreadback does not imply flat10 MHz RF response.

## Desktop/network limits and release

The host window manager stalled inGLXqueries, also blocking independent Qt/X11 probes. Under explicit user authorization, restartingXfce still stalled; the current `xfwm4 --compositor=off` process mapped windows. The saved `use_compositing=true` preference was restored; current-process compositing alone stayed off and existing apps remained.

At that time native X11 window reads returned black, while Qt application captures rendered completely. Physical screen visibility and human hearing were not marked verified. Audio automation was muted; actual speaker wiring/defaultIEC958suitability required user review. User systemproxyremained; GitHubfailurewas shown without persistent development tunnel.

All six frozen-source platforms passed applicable regression/archives/Unicode transactions before signing/publication. Windows 47 tests; others49; Ubuntu x64also7legacychecks. macOS actually executed audio/GUI pause checks; other hosted platforms without compatible default output skipped physical audio. LocalWSL/31.42executed it separately. Hosted skips are not audible-output passes.

Catalog1794324124merged80entries; 24toolbox/applicationpackages,5archives, catalogmade30assets. Actions archiveswere hash/ZIPchecked; package platform/version/source/size/SHA-256 and uploadedassetswere checked again before release. Signing keys stayed on the development host outsideGit/Actions/target.

31.42offline-imported four0.2.8packages through the same trustedcatalog, helperreturned `applied`;GUI PID 1153055, stable CLI/desktop/modules/signedrecords matched. GUI SHA-256 `1b6c60541fbffd3c512e322fb8cbe5e3fc139c537a6ee2bcd696f8316186518a` matched formal Debian. Test/old processes exited; ordinary formal GUI remained without test rate/frequency/language overrides. Settings/recordings/other toolboxes stayed.

Its actual updatecheck returned structured `network`/`Connection closed`, without a temporary proxy. Formal sustained FM used isolatedsettings/volume0, leaving user volume/squelch/device/frequency preferences intact.
