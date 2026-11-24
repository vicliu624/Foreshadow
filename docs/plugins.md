# Plugins, providers and the Foreshadow host

English · [简体中文](plugins.zh-CN.md)

**Foreshadow is a workbench built around plugins. Most signal-specific functionality is implemented by providers; the desktop brings those capabilities together into a shared research workflow.** Receiver support, detection, displays, the waveform laboratory, reference models, recovery, FM and television have owning modules and explicit contracts.

This guide describes the current implementation. The mandatory engineering rules remain [specification 16](16-extension-boundaries.md), [distribution specification 17](17-toolboxes-and-updates.md) and [workspace specification 18](18-desktop-workspaces.md).

## What belongs to the host

The host provides the common infrastructure that makes independent modules work together:

- Sample/stream/source contracts, codec services, clocks, source coordinates and discontinuity representation.
- Receiver sessions and leases, whole-demand conflict handling, bounded input delivery and lifecycle management.
- Recordings, replay, event storage, retention and generic source/evidence persistence.
- Dockable workspaces, automatic View entries, generic tool hosting, parameter controls and evidence views.
- Catalog trust, generic installation transactions, package-type adapters and application activation/rollback.

Shared DSP kernels implement operations such as filtering, resampling, reference fitting and evidence validation. Providers supply their domain algorithms and assumptions through those contracts. A specific receiver rule, modulation parameter or recovery algorithm does not belong in a central window merely because that window displays its result.

## Where the capabilities come from

The bundled [ADS-B module](adsb.md) adds detection, recovery and a live map panel.
Its integration and acceptance are in progress; the module guide separates
verified evidence from pending runtime and hardware checks.

| Provider family | Responsibilities and examples | Current integration |
|---|---|---|
| Receiver plugins | RTL-SDR/AIO2 and SDRplay; device discovery, capabilities, parameters, dependencies, recommendations, validation, application order, readback, driver runtime and translations | Independently built native modules discovered through the receiver C ABI |
| Detection modules | LoRa structure, narrowband modulation features, FM pilot evidence, CSS/PSK/OFDM/DTMB structural candidates; each owns input requirements, descriptions, evidence and resources | Registered built-in implementations assembled with the application |
| Display providers | Spectrum, waterfall and I/Q previews; display implementation, frame rendering and relevant presentation behavior | Built-in desktop display assembly |
| Live research providers | Waveform laboratory, FM receiver/audio and DTMB television; their panels, operations, state, background work, parameters and reception demands | Built-in `LiveResearchProvider` implementations, hosted by the generic workspace |
| Reference-model providers | Constant/tone, AM, FM, FSK, chirp, LoRa, PSK and OFDM reference generation; parameter schema, dependencies and whole-configuration validation | Registered research providers; generic host compares generated references with observed input |
| Recovery providers | FSK framing, LoRa PHY, an explicit paired-code family, trained PSK and OFDM; synchronization, channel/equalizer logic, symbols/bytes/frames, complex measurements and check evidence | Registered recovery providers; shared workers/UI/export consume the evidence protocol |
| External research tools | Native power report and other compatible snapshot tools; declared inputs, parameters, outputs, resources and runtime payload | Separate executable/manifest protocol and independently distributable research toolbox |
| Media codec providers | FFmpeg-backed decoders and the DRA core; track acceptance, capabilities, decoder construction and diagnostics | Registered native implementations inside the isolated media helper |
| Language resources | Provider text, parameter labels, descriptions and named placeholders, with per-key fallback | Files distributed with their owning provider; desktop language metadata uses its own discovery contract |

The directory name alone is not the boundary. For example, detection providers currently live under `src/adapters/modulation/`, while model/recovery providers live under `plugins/research/`. Both are extension owners. See the [source map](../src/README.md) for actual paths.

## Plugin, provider and toolbox mean different things

**Plugin** describes the product's modular capabilities. **Provider** is an implementation of a specific extension contract. **Toolbox** is a distribution/install/version unit. A module can be a plugin in the architectural sense while shipping inside the application package.

| Form | Can be installed independently today? | Requires rebuilding the host to add an implementation? | Activation/update behavior |
|---|---|---|---|
| Receiver native ABI module | Yes, as a receiver toolbox | No, when the existing ABI expresses the capability | Discovery at process startup; native changes take effect after restart |
| External snapshot research executable | Yes, as a compatible research toolbox | No, within the declared snapshot/output protocol | Managed resource/manifest discovery; running work and installation constraints apply |
| Built-in detector/display/live research/model/recovery/media provider | Ships with the application | Its assembly currently requires an application build | Updated with the application; applicable module selections remain available |
| Additional language resources | Resource additions through the owning contract | No algorithm/consumer change for a supported resource extension | Discovery/fallback follows that contract and installed packaging |

Consequently, a built-in analysis entry in Toolbox Center can contain many providers. It is not evidence that the analysis lives in the central window. Conversely, a registered provider is not automatically a downloadable dynamic library. Native receiver modules and external snapshot tools also have different lifecycle and capability contracts.

## How users encounter plugins

1. **Plugins → Toolbox Center** manages the trusted official catalog, installed packages, compatible updates, offline packages and pending activation. Check installed, loaded and pending versions separately.
2. **Module settings** controls the module selections exposed by the current application. Built-in capabilities are labelled as bundled and update with the application.
3. **Receiver settings** renders the selected receiver plugin's complete parameter descriptions. The provider evaluates pending dependencies, recommendations, blocking conflicts and application/readback.
4. **Detection** exposes detector descriptions and choices. Generic event views display provider labels, fields and features, including compliant unknown IDs.
5. **View** shows registered display/research panels. A live provider can add panels, actions and a declared workspace layout; resizing preserves the user's arrangement.
6. **Model comparison / Evidence and recovery** selects a research provider. Its parameters and validation drive the panel; generic plots/tables/export show the returned typed result.

Opening, closing or hiding a panel is separate from enabling a detector, installing a package or releasing a receiver lease. A hidden live tool can keep working. Freeze preserves a research observation without necessarily stopping other receiver users. Inspect the tool's actual state and stop operation.

## Example: adding a recovery method

Within the current recovery protocol, a new implementation supplies:

1. A provider with a stable identity, method/protocol versions and resource namespace.
2. Parameter descriptions and whole-pending-configuration validation, including dependencies and limits.
3. Its algorithm and typed evidence: observations, source support, stages, symbols/bits/bytes/frames, checks and optional complex measurements.
4. English resources, other language resources and fallback behavior.
5. Registration at the declared recovery assembly point, plus applicable build/resource declarations.
6. Independent numerical/negative/resource evidence through the existing worker, view, persistence and export consumers.

The host does not acquire `if method == new_id` branches. A capability that the current protocol cannot represent requires an explicit protocol and consumer upgrade. Static assembly is an intentional integration point, rather than a claim of independent binary installation.

The same rule applies to other change axes: receiver facts stay in receiver plugins; detector semantics stay in detection modules; model/recovery rules stay in research providers; sample interpretation stays in codecs. Do not turn every ordinary algorithm condition into a new plugin.

## Extension proof and operating limits

Tests exercise an independently built receiver without changing the host, independent detection/live-research/model/recovery/media implementations, resource-only language additions and fallback, generic parameter dependencies, unknown events, complex evidence, persistence and real toolbox transactions. Target/header architecture checks supplement those behavioral proofs.

A plugin boundary does not establish its physical performance. Device compatibility, successful module loading, confirmed modulation structure, recovered bytes, passed checks, EVM and device identity are different conclusions. Each provider exposes its supported scope; platform/package/hardware acceptance is recorded separately.

There is currently an official controlled distribution channel, without an arbitrary-source marketplace or third-party self-upload service. External snapshot tools do not automatically gain native live panels or receiver leases. New lifecycles need explicit protocol design.

## Implementation entry points

| Concern | Entry point |
|---|---|
| Receiver ABI and providers | `include/signalscope/adapters/receivers/plugin_api.h`, `plugins/receivers/` |
| Receiver discovery/loading | `src/adapters/receivers/receiver.cpp` |
| Detection registry | `src/adapters/modulation/registry/` |
| Desktop displays | `plugins/desktop/display/` |
| Live research assembly | `plugins/desktop/research/live_registry.cpp` |
| Model assembly | `plugins/research/models/registry.cpp` |
| Recovery assembly | `plugins/research/recovery/registry.cpp` |
| Media codec assembly | `src/adapters/media/codec_registry.cpp` |
| External research example | `plugins/external/power_report/` |
| Generic installation and packaging | `src/backend/distribution/`, `tools/distribution/`, `cmake/` |

See [receiver protocol](15-receiver-plugin-protocol.md), [reference protocol](23-reference-model-protocol.md), [recovery/evidence](24-recovery-and-evidence-protocol.md), [PSK/OFDM measurements](25-psk-ofdm-and-evm.md) and the [documentation index](README.md).

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
