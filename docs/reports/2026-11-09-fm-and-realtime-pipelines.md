# FM detection and real-time pipeline acceptance

English · [简体中文](2026-11-09-FM检测与实时流水线验收.md)

Historical report,9 November 2026, including0.2.5/0.2.6release follow-up. Later scheduling/media reports qualify later behavior.

## Reproduction and ownership

The user's100 MHz/10 MS/s/10 MHz screenshot showed only unverified chirp hints and analysis loss despite approximately5%whole-machine CPU. Target: Debian 13 x86-64,24 logical processors, RSPdx-R2. Main analysis discarded165,363,712/410,976,256complex samples; narrow analysis discarded300,204,032. The driver reported no rejected delivered input/timeouts but exposed no hardware counter.

The old eight-callback narrow queue buffered only about13.1ms with typical65,536-byte CI16 callbacks, while FM confirmation needed at least20ms continuous input. Serial DSP, repeated decoding, integer division and status waiting for DSP amplified gaps. FM returned only the first match per subband and tried at most three candidates, missing other stations.

## Corrections

Queues use actual payload-byte budgets, keeping default16 MiB despite short reads. Main/narrow/recording counts remain separate; a full recording queue stops explicitly without silent overwrite. Status reads completed snapshots and does not block acquisition/another pipeline. High-rate input merges bounded approximately5msanalysis batches, with gap reset/tail drain.

FM FFT/FIR share decoded input; contiguous FFT indices remove hot-loop modulo. LoRa cyclic indexing/history copies reduce duplication. Peaks below every downstream bound skip unnecessary neighborhood statistics while full spectra remain for fine synchronization; thresholds were not lowered. FM returns bounded multiple-station evidence and maintains repeated pilots per station; independent subbands run in parallel and merge deterministically. Host-recommended workers permit explicit1–32rather than a hard maximum4.

UI reports total-machine CPU/equivalent cores, dropped complex samples and MiB/seconds buffering, distinguishing analysis gaps from driver errors. Ownership is codec/detector/session processing; UI renders generic status. Specification16/AGEN TS gained the resource rules.

## Same-recording comparison

Actual two-second100 MHz/10 MS/s CI16 input was retained privately, outside releases.

| Scenario | Before | After |
|---|---:|---:|
| Offline confirmed FM stations |3|5|
| Paced confirmed FM stations |0|5|
| Paced FM narrow-stage rejected samples |15,166,720/20,000,000|0/20,000,000|
| All selected detectors on short input |Gaps in both analysis paths|Zero in both|

Stations were approximately95.806,97.001,98.198,100.799,102.211 MHz. Confirmation used repeated19 kHz stereo pilots; this does not cover every mono FM signal, audio quality or RDS.

A60secondactual run with default16 MiB and recommended10 LoRa workers accepted600,000,000 samples; recording and narrow analysis each processed all, narrow queue peak2,293,760 bytes/no rejects. Main LoRa processed594,576,896and rejected5,423,104/about0.90%. A separate64 MiB queue filled around45 seconds, proving extra buffering alone insufficient; 64 MiB was not published as default. Driver readback10 MS/s/8 MHzIFdoes not imply a flat10 MHz RF passband.

Tests added simultaneous stations, no-pilot negatives, actual-byte budgets, worker barriers/exception recovery and per-stage conservation. Existing LoRa weak/SFD negatives, modulation, recording, localization, workspace and distribution regressions remained. Platform fixes covered macOS resource location, Windows header ordering/macro conflicts and accidental Debug libarchive selection; Linux success did not qualify other packages.

## Startup recovery and releases

Replacing0.2.4 initially failed health activation and rolled back. The old version also segfaulted with the same settings; GDB pointed to Qt layout activation. Default rearrangement plus saved-layout restoration plus queued second restoration caused the failure. Valid saved layouts now restore once, retaining user settings. Two actual X11 start/exit checks and over ten minutes running passed.

Health receipt moved after initial event-loop activity; the helper also checks sustained process survival. A receipt-then-immediate-exit negative must roll back. Package tests isolate toolbox state. macOS copies registered modules/resources from one assembly set; Windows provider `*.runtime.json`schema1declares adjacent private DLLs, without developmentPATH. The backslash archive negative uses immutable USTARbytes because Windows PAXwriting normalizes separators; rejection rules stayed strict.

0.2.4left the formal update entrance; 0.2.5was a Debian 13recovery release. Final0.2.6source `9e154613ae49553603ab147a7a75aa536b43cf8a` passed [six-platform release validation](https://github.com/vicliu624/AMC/actions/runs/42621529629): Windows 43 tests; macOS/Ubuntu/Debian 45each; Ubuntu x86-64 also7legacy checks; real transactions and extracted execution passed. Native Windows paths/environment/Chinese installation were verified.

31.42updated through official online install/healthy activation. Actual GUI/stable entry/four managed packages and source/hash were checked. A final ten-second10 MS/s all-detector run processed100,000,000 samples in acquisition/recording/main/auxiliary, with zero software rejection and visible FM events. This short check does not supersede the60second0.90%LoRa loss or qualify mono FM/RDS/every signal. Distribution/cleanup/proxy details are in the [toolbox report](2026-11-09-toolbox-distribution.md).
