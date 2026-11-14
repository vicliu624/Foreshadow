# Architecture and interfaces

English · [简体中文](03-架构与接口设计.md)

The production stack is C++20/CMake with a Qt desktop. Python supports tooling and optional tests; it does not run the production acquisition/DSP path. The current directory/target map is [src/README.md](../src/README.md).

Foreshadow is a plugin-driven host. Providers own most domain-specific capabilities; sessions, generic DSP, persistence and desktop/CLI consumers supply shared infrastructure. The [plugin guide](plugins.md) maps these responsibilities to the current static, runtime and external deployment forms.

## Ownership and data flow

Receiver providers produce immutable encoded I/Q blocks through the public receiver/stream contracts. Reception sessions arbitrate whole-configuration demands and independent leases. Acquisition, recording, detection, live research and display have separate bounded lifecycles and counters.

Recorded source providers load segmented source windows using native paths. Generic DSP performs codec-based sample access, filtering, rational resampling, waveform/window analysis, reference fitting and recovery-result validation. Model and recovery providers own actual signal methods. Desktop and CLI consume generic descriptions/results.

| Change axis | Owner | Stable consumer / assembly |
|---|---|---|
| Hardware facts/parameters | Runtime receiver provider | Receiver registry/session; provider build registration |
| Detection semantics | Detection descriptor/provider | Generic event UI and snapshot scheduling; modulation registry |
| Source container | `SignalSourceProvider` | Window/analysis workers; source registry |
| Encoding/full-scale | Sample codec | DSP, recording and views |
| Reference generation | `ReferenceModelProvider` | Generic comparison/model UI; model registry |
| Synchronization/framing/checks | `RecoveryProvider` | Recovery validation/UI/export/save; recovery registry |
| Native live panels | `LiveResearchProvider` | Generic live host/workspace; `live_registry.cpp` |
| Distribution package type | Package adapter | Generic installer/catalog center; package-type assembly |

## Core contracts

Sample indices are integers and ranges are half-open. Source-stream positions, file offsets, processed clocks and local plot time are distinct. Rational clock mapping and filter support are explicit; discontinuities never become zero-filled observations. Timeouts use a monotonic clock, while any UTC anchor states its provenance.

Codec interfaces own byte counts, decoding, normalization and full scale. Current encodings include CU8, CI16-LE and CF32-LE; consumers reject unknown/truncated formats rather than treating non-CU8 as CI16.

Events retain generic labels, features and opaque fields. Recovery describes supported symbols/bits/bytes/frames/measurements and source dependencies; complex data uses recovery/evidence v2 and outer analysis v8. Older supported contracts remain readable. New capabilities require declared upgrades, not silent field loss.

## Resource and concurrency boundaries

Queues use actual byte/time budgets. Acquisition does not wait for optional research or GUI work. Recording exhaustion is explicit; analysis overload records unprocessed input, not “no signal.” Status copies already-published snapshots rather than waiting for another DSP pipeline. Immutable shared results are published atomically.

Long files are read in bounded windows; compute budgets cover FFTs, matrices, result capacities and JSON. Full-resolution valid data feeds measurements; drawing reduces only display primitives. Cancellation and source/session generations prevent stale work from replacing current results.

## Persistence and deployment

SigMF retains original/derived identity and parent support. Saved analysis describes applied processing and source identity, not an authoritative cached conclusion. Reopen recomputes and reports missing/changed sources. Workspace state restores once and resizing does not rebuild layout.

Signed catalogs, network transfer, package adapters, generic installation and application activation are separate protocols. Native resources and dependencies are declared at provider/build assembly points. New Mac bundles use `Foreshadow.app`; settings/toolbox/CLI identifiers retain compatibility.

Normative requirements: [extension boundaries](16-extension-boundaries.md), [distribution](17-toolboxes-and-updates.md), [workspace](18-desktop-workspaces.md), [laboratory](22-waveform-laboratory.md), [models](23-reference-model-protocol.md), [recovery](24-recovery-and-evidence-protocol.md), and [PSK/OFDM](25-psk-ofdm-and-evm.md).

## Independent receiver services (0.2.12)

The base application and optional SDRplay service-client toolbox are separate
delivery units. Vendor API/driver loading stays in the independently installed
receiver service. Provider-owned connection descriptors and request protocol 2
carry the address before enumeration; UI/sessions/CLI remain generic. The pinned
BSL SoapyRemote client needs SoapySDR 0.8.1 and supports the offline source override
`FETCHCONTENT_SOURCE_DIR_SOAPYREMOTE`. Only declared bundled providers enter the
base archive; raw CMake installation is a staging tree, not a qualified base
archive. See [receiver services](receiver-services.md) for SDRconnect assessment,
setup, protocol, continuity and license boundaries.
