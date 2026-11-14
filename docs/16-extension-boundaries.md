# Extension boundaries and the open/closed principle

English · [简体中文](16-扩展边界与开闭原则规格.md)

This specification governs engineering evolution. Stable consumers use replaceable providers through protocols; adding another implementation on the same change axis must not add consumer behavior branches. Directories, function pointers or classes called plugins do not alone demonstrate compliance.

## Owners, consumers and independent acceptance

| Change axis | Owner / assembly | Stable consumer | Required extension evidence |
|---|---|---|---|
| Receiver/device facts | `plugins/receivers/`, C ABI | Sessions, CLI, receiver settings | Independently build/install a third provider; discover/configure/recommend/capture/analyze with unchanged host hash |
| Language | Provider resource files | Resolver and generic controls | Add only files; language/subtag/per-key fallback works with unchanged binaries |
| I/Q encoding | `backend/iq` codecs | Detection, views, recordings, replay | A new encoding uses the same normalization/full-scale paths without consumer format branches |
| Detector/description | Modulation provider/registry | Scheduling, menus, assessment, results | Independent descriptor/detector enters all consumers without ID routing |
| Snapshot lifecycle | `SnapshotExtension` v2/registry | Snapshot analysis and stream subscriber | Independent periodic/energy strategy declares window/interval; matching-only dispatch, actual-byte budgets and nonblocking overload |
| Research tools/parameters | Desktop research provider/external package | Analysis/menu/background host | Provider supplies rendering/options/validation; host imports no built-in implementation |
| Live research lifecycle | `LiveResearchProvider` v3, `live_registry.cpp`, v1/v2 compatibility | LiveTools, subscriptions, workspace | Independent leases, arbitrary-start input, event/recording actions and multiple panels; automatic View; intact opaque fields; explicit tool-to-main-panel diagnostics |
| Receiver use/broadcast | `ReceptionDemand` / `ReceiverSession` | Monitoring/live host | Independent providers verify sharing, complete-demand conflicts, readback, release and bounded input |
| Source container | `SignalSourceProvider` v1, `backend/iq/signal_source.cpp` | Window/workers/source selection | Provider suffix/resources/reading and stored-to-source mapping; original vs storage encoding; independent container without format-ID branches |
| Reference model | `ReferenceModelProvider` v3, model registry, retained v1/v2 semantics | Comparison, parameter UI, workers, views | Independent generation/whole-config validation/resources enter the same fitting path; opaque methods survive; residual is not identification/EVM |
| Recovery/evidence | `RecoveryProvider` v1/v2, recovery registry | Validation, support tracing, workers/UI/export | Independent digital/measurement results preserve unknown fields, dependencies, candidate/truncated/check states; new capabilities upgrade the protocol |
| Event features | Producing provider/description | Results/evidence/store | Unknown compliant IDs still display labels/features/raw evidence |
| Output payload | Versioned research result protocol | Result host | Another provider of an existing capability needs no host edit; new capability explicitly upgrades the host/protocol |

## Responsibility rules

Generic UI does not own device rules, detector semantics or tool options. Consumers may interpret generic types/lifecycles/capabilities/errors, not identify an implementation to repair its rules. Recommendations are not the full legal capability set; host CPU is not hardware capability. Separate hardware validation, analysis compatibility and runtime load.

Keep requested, predicted and actual values distinct. Unknown facts remain unknown; providers disclose startup-confirmation scope. Calibration/ADC facts come through versioned readback/reception state (specification 21). Startup absence of overload does not qualify a whole selected interval. Clock correction, sample clipping and software rejection are not calibration/ADC evidence. Dynamic/interval telemetry requires an explicit upgrade.

Text protocols own keys and named placeholders. Business code passes keys/variables, not parallel English/Chinese sentences. Codec validation rejects unknown encodings, incomplete pairs and illegal values; no non-CU8→CI16 fallback. Names, parameters, assessment text, frozen preference aliases and event descriptions live with their implementation rather than parallel consumer lists.

## Permitted assembly

Add provider code/resources/build declarations at the stated registration point. Static built-in registration is permitted assembly, not permission to add ID branches throughout consumers. Runtime plugins must also demonstrate loading without rebuilding the host. Historical aliases remain a frozen migration scope.

Snapshot extensions register algorithms/descriptions/selections within their current input lifecycle; new lifecycle needs protocol design. ResearchProvider supplies rendering/controls. External snapshot tools declare parameters/dependencies/catalogs and receive an independent JSON settings file; generic resource installation follows declarations, never tool IDs.

Desktop language packs are discovered from installed `resources/desktop/languages` metadata (tag, native name, aliases, QM, optional Qt base). Plugin dictionaries are independent of desktop QM but share generic fallback. Adding a language must not edit a whitelist/menu branch.

New events provide `display_label`/structured `features`, or at least modulation/modulation_id. Existing legacy evidence mappings are frozen compatibility; architecture checks prohibit growing that consumer list. Algorithm-internal waveform/boundary/state conditions remain domain behavior; not every condition needs a plugin. Coherent devices/new payloads/new lifecycle are new host capabilities, not merely another model.

## Executable constraints

Keep native filesystem paths until UTF-8 boundaries; use `path_to_utf8`, `path_from_utf8`, and `environment_utf8`, not the Windows code page. Verify real archives in Chinese/non-BMP installation paths: application startup, modules and independent toolboxes, not just an ASCII build tree.

Target/header dependency checks restrict concrete imports. Architecture checks cover ID routing, duplicated sample decoding and embedded multilingual tables. Resources check keys/placeholders. Behavioral contracts use independent providers and resource-only additions. Existing modulation, recording, GUI, localization and physical acceptance remain applicable. Editing this specification does not excuse an implementation failure or justify loosening checks.

## Real-time/resource boundaries

Measure acquisition, recording, detection and display continuity separately. Analysis overload must not block acquisition or be labelled hardware loss. Without hardware counters report known reads/timeouts/rejections/continuity flags only.

Budget queues using actual payload and sample time, not USB callback count. Short reads must not shrink promised history. Full recording queues stop explicitly; buffers do not establish sustained throughput. Preserve every compliant multi-signal result instead of one slot per channel. Channels have independent algorithm state, merge deterministically, and respect explicit worker choices rather than a small host's budget as a universal limit.

Status reads published snapshots without waiting for another detector's DSP. Show total-machine CPU and equivalent cores, per-stage queue budgets and losses. Sustained/short-read/all-selected-detector/multi-signal/negative tests are required; short zero-loss input is not long hardware acceptance.

Live providers own byte budgets, continuity and output recovery. Audio underrun, I/Q rejection and stale PCM drops are separate; recoverable underrun is not demodulator failure. Stream/view contracts live in `backend/iq/stream.hpp`, below sessions. The former subscription header remains a source-compatibility entry.
