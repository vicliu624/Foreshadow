# Receiver plugins and open/closed-principle acceptance

English · [简体中文](2026-11-09-接收机插件与开闭原则验收.md)

Historical review,9 November 2026, covering runtime receiver plugins, resource localization and existing extension axes. Open/closed compliance applies within a declared capability contract; new lifecycles/result types can require explicit protocol upgrades.

## Reviewed ownership

| Axis | Problem found | Final boundary/proof |
|---|---|---|
| Receivers |Host linked vendor drivers and duplicated capability/rule knowledge |RTL/AIO2 and SDRplay became CABIplugins; independently built third module used only public headers/JSON and entered discovery/control/capture/analysis/UIwith unchanged host hash|
| Localization |Parallel languages embedded in constructors; fixed menu/fallback branches |Provider keys/resources and discovered language metadata; French-onlyresource increment/per-keyEnglishfallback without binary change|
| I/Q |Non-CU8 implicitly meant CI16 in several consumers |Codec table owns bytes/normalization/full-scale/invalid input; CF32 and independent receiver codec traversed capture/record/monitor|
| Detection |Parallel menu/evaluation/scheduler knowledge |Detector descriptions/resources/registry; independent detector entered all consumers without algorithm-IDbranches|
| Research |Panel imported concrete tools and FM parameters |Providers own parameter controls/validation/run; generic panel and external parameter/dependency/resources protocol, JSON settings file|
| Events |Unknown types lost labels/features |Generic `display_label`/`features`; frozen historical-level mapping cannot keep expanding|
| Enforcement |Architecture prose without behavioral extension proof |AGEN TS/specification 16/extension-contracts manifest/dependency checks/independent-provider behavior|

Plugins own complete parameters, dependencies, hardware validation, recommendation, apply order and readback. Host owns generic rendering/analysis adaptation/load feedback. Recommendations differ from hardware limits; blocking conflicts cannot apply.

## Regression and installation

LinuxRelease38/38 passed; installed receiver and complete desktop checks covered CLI record/replay/tools/research/GUI. Independent receiver passedASan/UBSan/leak checks. uConsole ARM64:35 passed,3 SDRplay-mockenvironment skips,0failures; installed desktop replay passed. Both physical receivers and the independent third provider passed English/Chinese settingsUI; the test provider was excluded from production. Resource paths with`..`and Debug/Release multi-config layouts were tested.

RSPdx-R2 serial24052AE770and AIO_V2Extserial25120901completed28wideband checks, plus8production-module/resource/advanced-rule/readback/no-host-driver-linkchecks. Full raw recording covered RSP 2/6/10 MS/s and AIO2.048/2.4/2.56/3.2 MS/s, complete search widths, FM andHFauto/I/Qreadback. RSP RF gain/IQcorrection/AGCtarget/Bias-T/notches/HDRapplication or disable rules were checked.

No actual driver-overflow case occurred in this batch. Correctly reporting/retaining partial input/stopping on overflow would not qualify losslessness. Five-second stress counts below keep separate analysis paths; their losses must not be added as raw-recording loss. Recording retained all software-delivered input.

| Case | Input/recording pairs | Main processed | Main rejected | Narrow skipped |
|---|---:|---:|---:|---:|
| RSP 10M, LoRa |50,000,000|7,553,024|42,446,976|0|
| RSP 10M, narrow |50,000,000|50,000,000|0|30,011,520|
| RSP 10M, combined |50,000,000|6,242,304|43,757,696|33,534,080|
| AIO2.56M, LoRa |12,800,000|12,767,232|32,768|0|
| AIO2.56M, narrow |12,800,000|12,800,000|0|0|
| AIO2.56M, combined |12,800,000|12,720,128|79,872|0|

These demonstrate wideband input/raw retention/visible gaps, rather than complete real-time detection on the uConsole.

## Historical deployment identity

Source snapshot `cd59a5868fec76d9f32b48b9f74cdfdc7f8d65edd829cf2847801a8787443259`; per-file manifest retained and report excluded from self-referential hash. Install `/home/vicliu/.local/opt/amc/receiver-cd59a5868fec`, actual GUI PID 169068matched current/stable entries and installed bytes.

| Artifact | SHA-256 |
|---|---|
|GUI|`5751a618ff1d808858b8fa53c6beed17e4d3176a0aeca3c0dabfc4c4a9e2e4d1`|
|CLI|`dc5345d967f21d26147b7b6d703b08036ebc8bfcaecff590a3d4b229e193cd74`|
|`amc-receiver-sdrplay.so`|`1d40e8b3ac2f3911121d5a778c0c81efc6a76e548fb199fea128902577bb8cdf`|
|`amc-receiver-rtlsdr.so`|`2deada2d4d4536e9ce48bcadd3c013b0337ecebbacb4039bd0e876377cbb06ca`|

Under the user's historical instruction, old processes/installs were replaced without backups; recordings remained. RSP 10 MS/s/10 MHz and AIO2.56 MS/s/2.56 MHz were saved; 3.2 MS/s remained selectable with risk warning.

## Boundaries and evidence

Receivers are runtime native modules; built-in detectors/research use assembly registries rather than all becoming runtime libraries. Codec changes belong in the codec table/contracts. Coherent multichannel, new lifecycle or result payload needs explicit versioned design. Configuration compatibility does not guarantee detection/decoding. RTLsoftware counts do not prove USB/hardwarecontinuity; sensitivity, field false-alarm rate, all-modulation payloads and long lossless operation were not qualified.

Windows/macOS status belongs to actual Actions results, without substituting Linux/ARM. Actions-budget blocks are external conditions, separate from source failures.

Private logs: `ocp-final-tests.log`, `ocp-package-tests.log`, `ocp-sanitizers.log`, `plugin-native-tests.log`, `plugin-independent-native.log`, `plugin-hardware-report.json`, `plugin-hardware-extras.json`, `plugin-deployment.json` and screenshots under ignored `build/`; remote `/home/vicliu/.cache/amc-wideband/`.
