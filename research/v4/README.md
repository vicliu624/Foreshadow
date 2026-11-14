# v4 weak-signal research snapshot

English · [简体中文](README.zh-CN.md)

This preserves the complete v4 source before the main tree returned to the validated v3.1 detector. Research on weak preambles, joint synchronization evidence and a common observation-interval model was retained; it is not counted as delivered or resolved.

`source-before-buffering.tar` contains the then-current CMakeLists.txt, include, src, tests and tools. SHA-256 is recorded in `snapshot.json`. Build directories, dependency sources and private keys are excluded. Extract into a new independent directory for a historical build; do not overwrite an installed receiver.

The snapshot used a0.5% transition-model advantage and failed the complete regression at weak noisy arrival offset0.67. An isolated `build/v4-margin-source` variant using0.1% passed four local tests, but an independent800-sample comparison found the same incorrect-SFD seed914001 falsely accepted by both thresholds. Comparison plans/results are private `build/rf-tests/v4-margin-holdout-*.json`. This candidate was not qualified on ARM or actual RF and was not a deployable version.

The production line returned to v3.1, which had native ARM and actual transmit/receive checks, while bounded buffering and recording integrity were repaired. Weak-signal recognition research remained unfinished. Failures, false positives and limits are retained in the diagnostic report rather than erased by the return to the validated baseline.
