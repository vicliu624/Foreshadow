# Foreshadow source map

English · [简体中文](README.zh-CN.md)

Paths below are relative to the repository root. The product is Foreshadow; `signalscope`/`amc` target names and public namespaces remain compatibility identifiers.

| Responsibility | Implementation | Build targets / assembly |
|---|---|---|
| Sample codecs, native paths, source windows and streams | `src/backend/iq/`, public contracts under `include/signalscope/backend/iq/` | `signalscope_iq` |
| Generic DSP | `src/backend/dsp/`: filtering/channelization, rational resampling, waveforms, window analysis, reference comparison, recovery validation, source support and export | `signalscope_dsp` |
| Sessions and receiver leases | `src/backend/sessions/`: receiver configuration, live monitoring, replay, snapshots, status and independent reception demands | `signalscope_core` |
| Recordings and event store | `src/backend/recordings/`: bounded recording, pre-trigger memory, ownership markers, quotas and event persistence | `signalscope_core` |
| Receiver protocol/loading | `src/adapters/receivers/` | `signalscope_receivers`; runtime ABI loading, rejection diagnostics and common entry points |
| Concrete receiver providers | `plugins/receivers/rtl_sdr/`, `sdrplay/`; independent fixtures under `test_receiver/` | `amc-receiver-rtlsdr`, `amc-receiver-sdrplay`; provider-owned rules/resources |
| Detection | `src/adapters/modulation/`: LoRa, narrowband FSK/OOK/ASK, FM, CSS, PSK, OFDM, DTMB; generic registry and snapshot scheduling | `signalscope_modulation`; assembly in `registry/` |
| Reference implementations | `plugins/research/models/` | `registry.cpp`; provider parameters, validation, generation and resources |
| Recovery implementations | `plugins/research/recovery/` | `registry.cpp`; bounded FSK, LoRa PHY, paired family, trained PSK/OFDM |
| Desktop host | `src/frontend/desktop/`: main window, menus, workspace, generic tool hosting, receiver controls, errors and localization | `signalscope-gui` |
| Shared parameter controls | `plugins/desktop/common/` | `signalscope_desktop_parameters`; renders provider descriptions and whole-draft dependencies |
| Desktop display providers | `plugins/desktop/display/` | `signalscope_desktop_display` |
| Desktop research providers | `plugins/desktop/research/`: built-ins, waveform laboratory, background workers, generic model/recovery panels and external-tool host | `signalscope_desktop_research`; `live_registry.cpp` assembles live providers |
| External research example | `plugins/external/power_report/` | `amc-power-report`; standalone native toolbox with declared resources |
| Television/media | `src/adapters/television/`, `src/adapters/media/`, `src/backend/transport/`, `src/backend/media/` | Bounded native receiver/media helpers; pinned dependencies assembled by `cmake/` |
| Distribution domain | `src/backend/distribution/` | Catalog trust, compatibility, installation records/transactions, activation protocols |
| Network adapter | `src/adapters/distribution/` | Qt transport/system proxy; separate from catalog and installer logic |
| Distribution operations | `tools/distribution/` | `signalscope-toolboxes`, `signalscope-update`, stable launchers; packaging/signing scripts |
| CLI | `src/frontend/cli/` | `signalscope`; command assembly and public service calls |
| Test firmware and host tools | `firmware/pager_tx/`, `tools/pager/` | Independent bounded test transmitter; ordinary tests/replay do not transmit |

## Plugin ownership and host infrastructure

Most product capabilities are implemented by the receiver, detection, display, live research, model, recovery and media providers listed above. Shared kernels and host services connect them. A provider owns its rules, parameters, resource keys and results; a directory name does not determine whether it is a runtime ABI module, built-in registration or external process. See the [plugin guide](../docs/plugins.md) for the user-facing map and deployment distinctions.

## Dependency direction

Desktop and CLI call session/application services and public provider contracts. Receiver vendor libraries are confined to their provider assembly. Detectors can use generic DSP; DSP does not import concrete receivers/detectors or session orchestration. Source/stream contracts live below the session layer, so research implementations need not depend back on sessions.

Model generation and receiver algorithms live in research providers. Generic comparison/recovery kernels validate typed data, budgets and source support. UI, export, save workers and evidence browsers consume protocols rather than model/recovery IDs. Adding a new payload capability requires explicit protocol versioning.

Root CMake assembles modules. `cmake/ModuleLinkRules.cmake` constrains target dependencies; `tests/check_module_dependencies.cmake` checks direct headers. `specs/extension-contracts.json` describes machine-enforced extension constraints. These checks complement behavioral independent-provider tests.

## Extension contracts

- [Receiver plugin ABI](../docs/15-receiver-plugin-protocol.md): discovery, parameters, actual values, runtime/resource declarations.
- [Extension boundaries](../docs/16-extension-boundaries.md): change ownership and stable-consumer rules.
- [Workspace/live research](../docs/18-desktop-workspaces.md): `LiveResearchProvider`, panels, leases, lifecycle and saved layouts.
- [Waveform laboratory](../docs/22-waveform-laboratory.md): segmented windows, native paths, clocks and analysis persistence.
- [Reference model protocol](../docs/23-reference-model-protocol.md): provider generation/parameters and generic fitting.
- [Recovery protocol](../docs/24-recovery-and-evidence-protocol.md), [complex evidence/EVM](../docs/25-psk-ofdm-and-evm.md): symbol/byte/frame/measurement data and traceability.
- [Distribution](../docs/17-toolboxes-and-updates.md): signed catalog, package adapters and generic transactions.

Desktop language-pack metadata is in `src/frontend/desktop/i18n/`; Qt translations are under `translations/`. Provider text lives in its own `i18n/`. Language fallback and generic text lookup are in the shared I/Q/text layer. File names such as `ams_zh_CN.ts` remain internal resource compatibility details.

External research tools use versioned snapshot IPC and declared resource/parameter metadata. This does not give them native live/multi-panel capabilities. Built-in C++ live providers and independently installable receiver ABI modules have different deployment contracts.
