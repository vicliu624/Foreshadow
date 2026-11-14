# Implementation status and risk decisions

English · [简体中文](05-实施计划与风险决策.md)

This is the current maintenance baseline, not the original calendar plan. Historical M0–M6 and early LoRa-only exclusions are superseded by the current requirements; their field evidence remains in dated reports.

## Delivered implementation

- Receiver runtime plugins, provider-owned whole-configuration controls/readback, wideband/multirate processing and generic detector/event descriptions.
- Independent 0.2.12 base and SDRplay service-client toolbox; independently installed SoapyRemote service, connection-aware receiver request protocol 2 and actual Windows/Linux r4 package/hardware qualification. See the [service report](reports/2026-11-14-receiver-service-review.md).
- GPL-3.0-or-later first-party application/plugins, per-package original notices/corresponding source/build and replacement material, archive revalidation and native license viewer.
- Managed toolbox distribution, signed catalogs, update activation/rollback, persistent desktop workspaces and bilingual resources.
- Independent FM reception and DTMB native transport/media pipeline.
- Waveform laboratory P0–P3: source windows/clocks, linked raw views, bounded capture/freeze, channel extraction, window analysis, reference models, recovery evidence, source navigation and applied-configuration save/reopen.
- Bounded FSK, LoRa PHY and explicit paired frame-family recovery; trained BPSK/QPSK and OFDM synchronization/channel estimation/equalization/data EVM.
- Native Windows/Linux review-package validation and Intel Mac acceptance/deployment, as recorded at their source identities.

The Foreshadow rename/documentation work is maintenance of this implementation. It does not itself publish a new signed release or requalify all deployed hardware.

## Remaining qualification boundaries

| Risk / unknown | Required evidence or decision |
|---|---|
| 24-hour lossless real-time/hardware continuity | Sustained physical input at each intended rate/workload; independent acquisition, recording and analysis counters |
| Weak/edge/overlapping detection | Controlled matrices, retained negatives, sensitivity/continuity qualification; no invented accuracy percentage |
| Arbitrary unknown protocol recovery | Not a current promise; each added receiver/framing capability needs its owned protocol/parameters and independent truth |
| DTMB local over-the-air programs | Actual full-passband antenna-to-TS-to-playback positive; PN/codec fixtures are not a substitute |
| Latest research on Apple Silicon/other Mac versions | Native build, UI, package/runtime and hardware qualification on those targets |
| Formal Foreshadow release | Frozen source, required platform tests, real packages, reviewed catalog and external-key signing/publication |
| Vendor API and embedded firmware | No vendor API/driver in base/client; user-installed service software retains its terms. Vendor redistribution and separate firmware clearance are not newly granted |
| Full-base television codec patents | [Actual free/open-source distribution assessment](codec-patent-assessment.md), specific live claims/territories and compatible grant or non-applicability evidence; kept separate from client/API/firmware decisions |

## Change policy

Identify axes/owners/consumers/assembly before editing. Preserve identifiers/data and frozen historical aliases. Require protocol upgrades for new result/lifecycle capabilities; do not route algorithms by IDs in stable UI/session/export/installer code. Add resource-only language and independent-provider evidence where applicable.

Daily CI uses a trusted self-hosted runner. Hosted matrices are explicit formal-release validation, not routine push builds. Private keys and RF data stay outside tracked source.

See [requirements](01-product-requirements-and-acceptance.md), [testing](04-algorithms-and-testing.md), [completion audit](reports/2026-11-12-waveform-completion-audit.md), [PSK/OFDM acceptance](reports/2026-11-12-psk-ofdm-acceptance.md), and [native Mac acceptance](reports/2026-11-12-native-macos-deployment.md). Each report's date and source identity govern its conclusions.
