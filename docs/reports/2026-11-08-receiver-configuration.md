# First receiver-configuration acceptance and uConsole deployment

English · [简体中文](2026-11-08-接收机配置第一批验收.md)

Historical acceptance,8 November 2026, Asia/Shanghai; final process audit22:01. Scope: RTL-SDR/RSP capabilities, existing dependencies, desktop feedback, application and driver readback. [Specification13](../13-receiver-capabilities-and-configuration.md) defines ownership. Vendor libraries stayed in receiver adapters; Qt stayed out of receiver/analysis core.

## Delivered behavior

Both backends supplied attributed capabilities, separating recommendations from legal device ranges so high RSP rates were not filtered by presets. Desktop, CLI preflight and monitor startup shared hardware/analysis rules and distinguished constraints, detector requirements and performance notes. Settings linked rate/frequency/antenna/gain/search/detectors and displayed predictedIF, nominalADCbits, codec/data volume.

Apply called the adapter and read actual values before saving. RSP briefly activated the stream to confirm configuration without recordingI/Q; monitoring reconfigured/reconfirmed. RTLautomatic-gain mode without readback was marked driver-accepted rather than falsely read. Session/capture retained request/actual values. CLI added `validate` and `configure`.

## Source and regression

The259-file working-source snapshot was not a Gitcommit: `dd605a0f5401d6e4ae849f7bcef3d23ec4434a43c63764e44b4a8aa105f62a95`. Every target file was SHA-256 verified.141existing artifacts were cleared before a full native rebuild. Cache name `f059399218b4` was an initial staging identifier; final `AMC_SOURCE_MANIFEST.json` defines identity.

| Installed file | SHA-256 |
|---|---|
|GUI|`2bdfeac1246cd4ba441e9392610d60c7cc3a375c3a6b73650ee0243946021678`|
|CLI|`2df6616f46b29fa5ff65e13905c4acc8362ab307be0e5268b451d6705afc481c`|

| Environment | Result |
|---|---|
|LocalWSL Ubuntu 24.04x64/Qt 6|26 passed|
|CLI without Qt/RTL/Soapy|Built; missing backend explicit|
|Clean uConsole Debian 13 ARM64/Qt 6.8.2|24 passed,2conditional skips,0 failed|
|Physical RSP settings|Explicit hardware mode passed, additional to default CTest|

ARMmock SDRplay backend/settings skipped because the real vendor module was installed. Local isolated mocks passed; actual hardware separately covered query/preflight/apply/start/readback/capture/UI. Coverage included tuner ranges/rate gaps/legal nonrecommended rates/antenna edges/gain/LoRa timing/passband/ADCcontainer distinction/serialization/ignored settings/valid-config-but-start-fails/close protection. Windows/macOS native packages were not accepted in this batch. NoRTLdevice enumerated remotely, so mockRTLrules were not physical RF acceptance.

## Physical RSPdx-R2

API 3.15, native CS16. Twelve CLI checks covered device/analysis distinction,2.048 MS/s LoRa timing conflict and antennaCfrequency limits.2/6/10 MS/s applied/read back with startup confirmation and IF1.536/6/8 MHz respectively. AntennaB,470.5 MHz, automaticgain and0.1ssettling yielded:

| Rate | Seconds | Complex samples | Bytes | State |
|---|---:|---:|---:|---|
|2 MS/s|1|2,000,000|8,000,000|complete|
|6 MS/s|0.5|3,000,000|12,000,000|complete|
|10 MS/s|0.5|5,000,000|20,000,000|complete|

CI16 uses4 bytes/pair. The10 MS/s displayed8bitADCvalue was vendor-spec-derived, not measuredENOB, and did not change CS16 storage. Three-seconddefault LoRa monitoring consumed6,000,000 pairs in capture/main/recording with no main rejection, without all-detector throughput qualification.

PhysicalUIchecked2.048 MS/s timing, antennaC200 MHz boundary and10 MS/s format/resolution/volume, then applied3 MS/s/AntennaB/32 dBIFgainreduction and confirmed readback/start. These short functional checks did not prove long-term USB continuity, sensitivity or accuracy; vendor flags/software counts were the available continuity evidence.

## Problems retained and deployment

The RSP was visible on USB but initially absent from API enumeration; restarting existing `sdrplay`service restored it without SDK replacement. An old GUI used about301.4%CPU over a two-second observation and a paced narrowband test missed amplitude events. After stopping that to-be-replaced process, identical code/rate/assertions passed three times and final clean regression passed. Preserved archive mtimes also caused an incremental build to miss changed targets and physicalUIto skip; only the clean rebuilt result supports final acceptance.

Install `/home/vicliu/.local/opt/amc/receiver-dd605a0f5401`. Stablecurrent/CLI/GUI and `amc-rspdx-r2.desktop`, `signalscope.desktop`, `~/Desktop/SignalScope.desktop`pointed to it. Old PID 2310and`baaddb9`installation were removed without backup under the user's instruction; independent recordings stayed. Final GUI PID 38840matched installed bytes, with no old path references. It was idle on SDRplay/2 MS/s/automaticgain/478.875 MHz.

Private evidence: `build/uconsole-tests.log`, `uconsole-clean-build.log`, `uconsole-hardware-report.json`, `uconsole-physical-ui.log`, `uconsole-deployment.json`, `uconsole-final-audit.json`and two screenshots; remote `~/.cache/amc-receiver-f059399218b4/`.

At this batch, monitoring retained a separate3.2 MS/s software limit. RF/LNA/Bias-T/notches/HDRand high-rate channelization/replay/detection/audio were later batches. Later specifications supersede those historical software limits without changing this report's evidence.
