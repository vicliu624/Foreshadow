# Historical v3 periodic-FSK direction-change correction

English · [简体中文](README.zh-CN.md)

This is the preserved v3.1 candidate patch, independent of v4 weak-signal experiments. The chronology below records the candidate before deployment and its subsequent partial ARM validation. It is not a patch to apply directly to today's reorganized source tree.

The deployed v3 detector incorrectly emitted LoRa structures for two independently generated periodic-FSK inputs. This patch requires the SFD downchirp match to exceed the same reverse-direction projection during the preamble by at least5 dB, before the existing transition check. Preamble, synchronization, quarter-window, candidate activation and end-position advantage thresholds are unchanged.

`fix.patch` changes historical `src/lora.cpp`, `src/monitor.cpp` and `src/gui.cpp`; the GUI change includes the algorithm marker `dechirp-fft-structure-v3.1` and the refresh correction described below. `baseline-sha256.json` binds the original bytes. Three `.v3-base` source files and `lora_test.py.v3-base` retain the matching baseline.

## Initial validation

- `git apply --check --ignore-space-change --directory=build/baseline-v3-source patches/v3-periodic-fsk/fix.patch` passed.
- An isolated local build and the original v3 LoRa regression passed.
- A240-input comparison used identical generator settings and per-input SHA-256. Three positive groups retained29/30,4/30 and0/30 detections. Periodic-FSK false structures fell from2/30 to0/30; four other negative groups remained zero.
- Private result: `build/rf-tests/v3-fsk-fix-comparison.json`. This does not measure RF sensitivity or field false-alarm rate.

## Historical reproduction

Copy the corresponding historical CMakeLists.txt, include, src, tools and tests into an independent source directory. Restore `lora.cpp.v3-base`, `monitor.cpp.v3-base` and `gui.cpp.v3-base` as its `src/lora.cpp`, `src/monitor.cpp` and `src/gui.cpp`; restore `lora_test.py.v3-base` as `tests/lora_test.py`. Verify all three source hashes, then apply from the repository root:

```sh
git apply --ignore-space-change --directory=build/baseline-v3-source patches/v3-periodic-fsk/fix.patch
```

The patch uses LF while baseline files have mixed endings, hence `--ignore-space-change`. The original isolated source/build were `build/v3-fsk-fix-source` and `build/v3-fsk-fix-build`. ARM used `/home/vicliu/signalscope-fsk-fix-v31` with Linux idle scheduling priority; its results were not evidence for a different installed binary.

## Failed long observation and GUI follow-up

The original installation reported analysis drops around minute33; its observer failed and exited. It was therefore no longer a passing24-hour observation. The independent ARM build was stopped to preserve the scene.

The GUI rebuilt the event table ten times per second although status changed approximately once per second. The candidate deduplicated status/spectrum sequence numbers, reused items and coalesced refreshes. At that point it had not yet run on ARM; earlier detector-only timing could not qualify the enlarged patch.

`tools/deploy_receiver.py` could select `--build-dir` and `--expected-algorithm`. Deployment gates were tightened to require `--validation-report` and `--validation-note` for every deployment, including at least300 seconds of actual-device accounting for the same binary. Gate tests and `--help` passed. Historical static notes could no longer authorize a different SHA-256; no installation was performed by this patch record.

## Subsequent ARM results

The two fixed FSK negatives passed on the ARM v3.1 CLI with matching sample counts, algorithm identity and hashes (`build/rf-tests/v31-fsk-regressions-arm.json`). A300-second physical run with that binary accounted for525,000,000complex samples in acquisition, analysis and recording, with zero software queue drops and no pending tail. Five selected configurations repeated twice plus six controls gave sixteen associated nominal−9 dBm transmissions and expected full-frame I/Q retention. Payload and CRC were not verified. Evidence names: `v31-live300.json`, `matrix-v31-fskfix-focused-repeat2.json`, `matrix-v31-fskfix-controls.json`.

The GUI linked and completed offscreen short-recording replay, screenshot and raw-data-preservation checks. In two ten-second static-refresh windows, main-thread CPU fell from about0.70s to0.16s. Both ran under the same10%-of-one-core quota, so this does not predict full-machine real-time CPU or losslessness. `v31-gui-replay-offscreen.png` was visually inspected. At the final entry in this record, complete ARM regression, installation of the new GUI, live UI load and a new long observation remained pending. Later deployment evidence belongs to its later report.
