# Receiver settings and modulation-assessment acceptance

English · [简体中文](2026-11-08-接收机设置与调制评估验收.md)

Historical report,8 November 2026: recommendations, preset/custom widths, parameter dependencies, repair actions, strict apply validation, adaptive two-region settings and modulation assessment were completed/deployed to uConsole.

## Interaction

Settings preferred about1200pxwithin available screen, side-by-side parameters/assessment; below1000pxregions stacked with splitters/scrolling. Main/settings shared recommended/common/current-maximum/custom width. Fixed/custom invalid requests remained unchanged; only maximum mode followed passband changes.

Compatible rates were shown first; advanced controls retained all device-supported/custom integer rates. The then-current recommendation preferred2 MS/s, at most1 MHz search and automaticgain, chose a compatible antenna when needed and retained frequency/detector selection. Shared session preflight had to pass. This historical recommendation is superseded by the later wideband provider rules.

Conflicting fields showed requested values/repair. Invalid whole plans disabled Apply and were rechecked at entry. During application, edit/cancel were disabled; values saved only after successful readback. Technical details started collapsed. Refresh/device switch cleared old assessment immediately, preventing stale compatibility after query failure.

Ten rows covered LoRa,2-FSK/GFSK, OOK, ASK, broadcast FM, BPSK, QPSK,4-FSK, OFDM, CSS with actual enabled state, conditional compatibility, implemented parsing scope and impact. Full descriptions expanded/tooltipped. Test styling matched the main dark theme; real font/header sizing prevented English-header clipping.

## Regression

Local Linux/Qt 29/29 passed, final61.82s; driver-free/non-Qt CLI also built. Native Debian ARM64/Qt 6.8.2:26 passed/3skipped/0 failed,127.61s. `sdrplay_backend_contract`, English/Chinese desktop mocks skipped because real vendor loading could not be isolated; explicit physical English/Chinese settings tests separately passed.

Coverage includedRTL/RSP recommendation and impossible plans without retuning/disabling detectors; requested5000 kHz at2 MS/s retained with1036 kHz limit, blocked until repaired to1000 kHz; LoRa timing/high-rate-analysisconflicts/invalidcustomrates; antennaC200 MHz boundary/compatible repair; combined conflicts/gain; max-mode dependencies/custom retention/downward rounding/restoration; detector scopes/FM band overlap/full-band OFDM; refresh error clearing; 1200/780pxlayout; both languages/readback/theme.

## Physical hardware and deployment

RSPdx-R2 serial24052AE770/API 3.15. Both languageUIsapplied3 MS/s/AntennaB/32 dBm anualIFgainreduction, confirming activation/rate/1.536 MHzIF. Twelveadditional CLI checks covered capabilities/conflicts/2/6/10 MS/s application/readback/raw input/short monitoring. Raw cases: 2 MS/s×1s=2,000,000 pairs/8,000,000 bytes; 6 MS/s×0.5s=3,000,000/12,000,000;10 MS/s×0.5s=5,000,000/20,000,000. Three-second2 MS/s default LoRa run accounted6,000,000pairsin capture/main/recording with zero main rejects, without narrow analysis/all-detector sustained qualification.

267-file source snapshot `85b2b8e432839da6184ff9bfc75f4ee361456edb338e200e65586b732d3d1f65`; report added afterward. Install `/home/vicliu/.local/opt/amc/receiver-85b2b8e43283`; current/stableentries/three desktop launchers checked. Actual PID 49879resolved to the install and matched GUI SHA-256 `2acf205fdb32a345fc661f4724f2c344b451dbf6aebb4bcdd735f4a06325bb55`;CLI `04c6ff390801fd4aca0a86c62eaac4be31441b3b9b4d732dd82bf3d56c17099e`.

Old GUI processes and `receiver-dd605a0f5401`/pre-theme`receiver-818e175978dc`installs were removed without backup; recordings remained elsewhere. Audit23:01Asia/Shanghai visually checked the actual settings window with conflict-free2 MS/s/1 MHz/automaticgain.

Private `build/settings-*`and `/home/vicliu/.cache/amc-settings-c66a3d7138ad`retain logs/hardware/deployment/both-language screenshots. Cache name is initial; final manifest identifies85b2b8e…. Source timestamps were refreshed between stopped/completed builds to prevent archive-mtime omissions. Finalregression `theme-tests.log`, hardware `hardware-theme/report.json`.

Assessment proves configuration conditions, without air-interface success probability. At that date LoRa did not recover syncword/payload; narrow classes yielded candidates/features, OFDM/CSS structures and FM pilot/audio. Later research recovery is separate capability. Sensitivity, field false positives, complete protocols and long continuous operation were not qualified.
