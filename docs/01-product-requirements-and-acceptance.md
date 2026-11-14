# Product requirements and acceptance

English · [简体中文](01-产品需求与验收.md)

Foreshadow is a cross-platform signal research workbench. This current baseline replaces the original uConsole-only/LoRa-only scope. Dated reports retain what was implemented and tested at each historical stage.

## Product outcomes

1. Acquire an effective wideband window from a runtime receiver provider; display requested, compatible and actual settings separately.
2. Observe spectrum, waterfall, candidate events and operational counters without blocking acquisition.
3. Record original I/Q manually or from supported event triggers, enforce bounded memory/disk policy, and replay in software.
4. Open recorded data or freeze live input; inspect samples, isolate a channel, measure windows, compare explicit models and run supported recovery methods.
5. Follow a conclusion, byte, frame or complex measurement back to its source dependencies; preserve assumptions, failures, unknown values and validation states.
6. Save/reopen analysis configurations and export evidence in a reproducible form.
7. Support English/Chinese resources, dockable persistent workspaces, controlled toolbox distribution and safe application activation.

## Feature acceptance matrix

| Capability | Required evidence | Current reference |
|---|---|---|
| Receiver discovery/configuration | Independent provider, full pending-config dependencies, startup/readback and actual device evidence | Specifications 13–16; receiver reports |
| Wideband detection | Independent truth, false positives, multiple signals, passband limits, bounded sustained processing and per-stage counters | Specifications 14/16/19; FM/DTMB reports |
| Recording/storage | Exact data roundtrip, partial/error states, ownership-safe eviction, pre-trigger truncation, quota/reserve and export | Specification 08 and core/CLI tests |
| Workspace | Automatic panel discovery, explicit presets, resize preservation, restart persistence, wide/1.25-scale/640×480 native UI | Specification 18 and native reports |
| Waveforms/channels/windows | Independent formulas, encodings, gaps, clocks, filter support, precise cursor/mapping and full-resolution computation | Specification 22 and waveform contracts |
| Models | Independent providers/resources, explicit fitting freedoms, negative models, real UI/save/reopen | Specification 23 and reference contracts |
| Recovery | Known independent bytes, errors/truncation/unknown checks, exact support, exports and independent result providers | Specification 24 and recovery contracts |
| PSK/OFDM | Independent transmitter truth, training/data isolation, impairments, EVM recomputation, erasures and budgets | Specification 25 and coherent recovery contract |
| Distribution | Signatures, hashes, compatibility, generic transactions, restart activation and failed-start rollback | Specification 17 and real package checks |
| macOS | Native compiler/runtime, complete package closure, Cocoa UI and real installation | Specification 26 and native Mac report |

## Interpretation requirements

A candidate is not a device identity or named application protocol. Structure matching is not CRC-validated payload recovery. A fitted reference is generated data; residuals do not automatically constitute EVM. EVM does not establish application validation or BER. Application checks and PHY checks remain separate.

Power uses declared digital units and normalization, not calibrated dBm. Original sample time, host observation, save time and replay progress remain distinct. Missing intervals, unobserved hardware loss, low-amplitude invalid phase and unsupported payload capabilities remain explicit rather than fabricated.

Continuous operation until stopped is supported; general 24-hour lossless processing and hardware-no-loss remain unproven. One current receiver window is simultaneous; retuning creates coverage gaps. Automatic original-I/Q event storage is currently triggered by LoRa structure evidence, not every detector.

## Completion policy

Implementation, automated contract validation, native UI validation, actual package validation, deployment and formal publication are separate states. A requirement is accepted only with environment, source identity, input provenance, command/procedure, result and remaining limits. Historic package success does not automatically qualify the latest branch.

See the [README](../README.md), [implementation/risk status](05-implementation-and-risks.md), [PSK/OFDM report](reports/2026-11-12-psk-ofdm-acceptance.md) and [native Mac report](reports/2026-11-12-native-macos-deployment.md). Hardware transmit testing is confined to the separate bounded Pager workflow; application replay never retransmits RF.
