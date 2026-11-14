# Foreshadow documentation

English · [简体中文](README.zh-CN.md)

Foreshadow is a plugin-driven signal research workbench. Receiver, detection, display, live research, model, recovery and media providers deliver most domain-specific capabilities. The host supplies shared protocols, sessions, storage, workspaces and controlled distribution.

Current documentation describes the source branch. Dated acceptance reports identify their own source, package and hardware; published releases can have a different baseline. Read failures, skips and external conditions with each recorded result.

- [Licensing scope](../LICENSING.md), [third-party notices](../THIRD_PARTY_NOTICES.md), and [source/replacement instructions](../SOURCE_AND_REPLACEMENT.md).

- [Distribution compliance material protocol](distribution-compliance.md).

- [GPL adoption and Windows/Linux r3 qualification](reports/2026-11-13-license-adoption-review.md).

## Start here

- [Product overview and complete workflow](../README.md)
- [Plugin families, host responsibilities and deployment](plugins.md)
- [English UI screenshots from the receiver-connected workstation](screenshots.md)
- [Delivery and user guide](09-delivery-and-user-guide.md)
- [Build and CLI](07-building-and-cli.md)
- [Source ownership map](../src/README.md)
- [Licensing assessment, Qt scope and distribution gaps](licensing-assessment.md)
- [Actual free/open-source codec patent assessment and inquiry scope](codec-patent-assessment.md)
- [Mandatory engineering constraints](../AGENTS.md)

## Current requirements and protocols

| Document | Status |
|---|---|
| [Product requirements and acceptance](01-product-requirements-and-acceptance.md) | current |
| [Architecture and interfaces](03-architecture-and-interfaces.md) | current |
| [Algorithms and testing](04-algorithms-and-testing.md) | current |
| [Implementation status and risk decisions](05-implementation-and-risks.md) | current |
| [Building and command-line operation](07-building-and-cli.md) | current |
| [Continuous monitoring and event storage](08-continuous-monitoring-and-event-storage.md) | current |
| [Delivery and user guide](09-delivery-and-user-guide.md) | current |
| [Receiver capabilities and configuration dependencies](13-receiver-capabilities-and-configuration.md) | current |
| [Wideband reception and multirate analysis](14-wideband-and-multirate-analysis.md) | current |
| [Receiver plugin protocol and extension](15-receiver-plugin-protocol.md) | current |
| [Extension boundaries and the open/closed principle](16-extension-boundaries.md) | current |
| [Controlled toolboxes and online updates](17-toolboxes-and-updates.md) | current |
| [Desktop workspaces and module interfaces](18-desktop-workspaces.md) | current |
| [DTMB structure detection](19-dtmb-detection.md) | current |
| [DTMB transport recovery and television playback](20-dtmb-television.md) | current |
| [Independent broadcast sessions and runtime status](21-independent-broadcast-sessions.md) | current |
| [Waveform laboratory and traceable analysis](22-waveform-laboratory.md) | current |
| [Reference model and comparison protocol](23-reference-model-protocol.md) | current |
| [Recovery and traceable evidence protocol](24-recovery-and-evidence-protocol.md) | current |
| [PSK / OFDM reception and EVM](25-psk-ofdm-and-evm.md) | current |
| [Native macOS build and deployment acceptance](26-native-macos-acceptance.md) | current |

## Historical design and research background

| Document | Status |
|---|---|
| [Hardware and environment investigation](02-hardware-and-environment.md) | historical |
| [Chinese LoRa applications and observation presets](06-china-lora-presets.md) | historical |
| [821 MHz reception migration request](10-821mhz-reception-migration.md) | historical |
| [Sub-GHz modulation research](11-subghz-modulation-research.md) | historical |
| [Cross-platform desktop design record](12-cross-platform-desktop-design.md) | historical |

## Dated acceptance and investigation reports

| Document | Status |
|---|---|
| [First acquisition and environment record](reports/2026-11-03-first-capture.md) | historical |
| [Low-power short-burst diagnostics](reports/2026-11-04-low-power-burst-diagnostics.md) | historical |
| [Passive investigation of unverified candidates](reports/2026-11-04-unverified-candidates.md) | historical |
| [LoRa recovery trials and weak-signal baseline](reports/2026-11-05-lora-recovery-and-weak-signals.md) | historical |
| [Receiver settings and modulation-assessment acceptance](reports/2026-11-08-receiver-settings.md) | historical |
| [First receiver-configuration acceptance and uConsole deployment](reports/2026-11-08-receiver-configuration.md) | historical |
| [FM detection and real-time pipeline acceptance](reports/2026-11-09-fm-and-realtime-pipelines.md) | historical |
| [Two-receiver wideband acceptance](reports/2026-11-09-wideband-receivers.md) | historical |
| [Online updates and controlled-toolbox release acceptance](reports/2026-11-09-toolbox-distribution.md) | historical |
| [Receiver plugins and open/closed-principle acceptance](reports/2026-11-09-receiver-extensions.md) | historical |
| [FM playback recovery and module controls acceptance](reports/2026-11-10-fm-playback-recovery.md) | historical |
| [LoRa real-time loss and shared scheduling acceptance](reports/2026-11-10-lora-scheduling.md) | historical |
| [DTMB television playback and 0.2.10 release acceptance](reports/2026-11-11-dtmb-television-release.md) | historical |
| [DTMB daytime rescan and candidate review](reports/2026-11-11-dtmb-daytime-rescan.md) | historical |
| [DTMB program decoding: development-stage verification](reports/2026-11-11-dtmb-decoding.md) | historical |
| [DTMB structure detection and on-air survey acceptance](reports/2026-11-11-dtmb-detection.md) | historical |
| [Waveform laboratory implementation and acceptance record](reports/2026-11-11-waveform-implementation.md) | historical |
| [PSK/OFDM reception, equalization and EVM acceptance](reports/2026-11-12-psk-ofdm-acceptance.md) | historical |
| [Native macOS acceptance and review deployment](reports/2026-11-12-native-macos-deployment.md) | historical |
| [Waveform laboratory completion audit](reports/2026-11-12-waveform-completion-audit.md) | historical |
| [Foreshadow rebrand, plugin documentation and review maintenance](reports/2026-11-13-foreshadow-maintenance.md) | historical |
| [PCM playback and licensing-review qualification](reports/2026-11-13-pcm-and-licensing-review.md) | historical |
| [GPL adoption and Windows/Linux r3 review qualification](reports/2026-11-13-license-adoption-review.md) | historical |
| [Independent base, receiver service and Windows/Linux r4 qualification](reports/2026-11-14-receiver-service-review.md) | historical |
| [Corrected source commands, new archives and separate public-release preparation](reports/2026-11-14-public-release-preparation.md) | historical |

## Decision records

| Document | Status |
|---|---|
| [Conditional SFD tolerance and weak-signal candidates](adr/0001-conditional-sfd-and-weak-signals.md) | decision |

## Firmware, fixtures and dependency provenance

| Document | Status |
|---|---|
| [Pager serial-controlled low-power LoRa and GFSK firmware](../firmware/pager_tx/README.md) | current |
| [Historical v3 periodic-FSK direction-change correction](../patches/v3-periodic-fsk/README.md) | historical |
| [v4 weak-signal research snapshot](../research/v4/README.md) | historical |
| [Backslash archive-path rejection fixture](../tests/fixtures/distribution-backslash.md) | fixture |
| [Independent offline DTMB waveform](../tests/fixtures/dtmb-waveform/README.md) | fixture |
| [High-edge coherent-preamble recording](../tests/fixtures/lora-edge-high-coherent-prefix/README.md) | fixture |
| [High-edge SFD-transition recording](../tests/fixtures/lora-edge-high-sfd-transition/README.md) | fixture |
| [Low-edge fine-synchronization recording](../tests/fixtures/lora-edge-low-fine-sync/README.md) | fixture |
| [Actual SF7 recording with a weak SFD](../tests/fixtures/lora-sf7-weak-sfd/README.md) | fixture |
| [Periodic-FSK negative that v3 classified as LoRa](../tests/fixtures/periodic-fsk-v3-false-1/README.md) | fixture |
| [Second periodic-FSK negative that v3 classified as LoRa](../tests/fixtures/periodic-fsk-v3-false-2/README.md) | fixture |
| [Synthetic television programs](../tests/fixtures/television-programs/README.md) | fixture |
| [Native television dependencies](../third_party/README.md) | current |
| [DRA decoding core](../third_party/dra/UPSTREAM.md) | current |
| [Changes applied to pinned dtmb-sdr sources](../third_party/dtmb-patches/README.md) | current |

## Maintaining this documentation

Every Markdown document has an English default and Simplified Chinese counterpart, indexed by `documents.json`. Update both, retain historical identities and run `python tools/docs/check_docs.py` for inventory, language navigation, links, anchors and PNG validation.

- [Receiver services and the independent base package](receiver-services.md) — current SDRplay service boundary, setup and control/continuity limits.

- [Repository transition and source-only publication](repository-transition.md)
