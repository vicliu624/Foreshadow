# Independent broadcast sessions and runtime status

English · [简体中文](21-独立广播会话与状态栏规格.md)

## Use and compatibility

Broadcast and wideband discovery are parallel entry points. FM/DTMB can tune, name favorites and play without starting monitoring; provider-owned event actions add favorites. Providers declare input demands and receiver plugins propose legal configurations. Full manual configuration/readback/advanced diagnostics remain available.

Favorites are versioned stable-ID user data: FM frequency/name, DTMB multiplex frequency/program identity rather than changing media PIDs. Preserve old preferences. Temporary playback must not overwrite saved wideband configuration.

| Axis | Owner | Stable consumer |
|---|---|---|
| Input band/minimum rate/demodulation | Broadcast provider | Generic live host |
| Hardware validity/recommendation/readback | Receiver provider | Session's generic demand evaluation |
| Ownership/sharing/release/conflict | Generic receiver session | Independent monitoring/research leases |
| Favorites/search/programs/playback | FM/DTMB provider | Workspace/menu without module IDs |
| Runtime summary/navigation | Generic status descriptors | Status bar renders and invokes actions |

## Lifecycle and startup facts

Live protocol v2 adds on-demand acquisition/release; v1 retains monitor-bound context. Independent tools do not open vendor drivers. Cross-thread callbacks isolate exceptions, destroyed widgets and expired generations. Acquisition callbacks never run UI or wait for demodulation/audio.

Demands/leases are `backend/iq/reception.hpp` contracts, below session/UI; ReceiverSession owns physical arbitration. Optional navigation callbacks request receiver selection without importing receiver dialogs. v1 cannot pretend to offer v2 acquisition/summary/navigation.

ReceiverReadback/Ready JSON v2 and ReceptionStatus v2 add AcquisitionConditions v1 owned by backend/iq and populated by providers. Ready occurs once before first samples; C ABI 1 function table is unchanged. Undeclared Ready version means legacy v1 with unknown new facts. Declared unsupported versions or contradictory facts fail, not silently downgrade. This is not dynamic telemetry.

Fact identity is `org.amc.acquisition-conditions` v1, scope `before_first_samples`, origin `provider|not_reported`. Frequency/amplitude calibration independently use unknown/uncalibrated/calibrated/not_applicable; calibrated/not-applicable requires evidence. ADC monitor capability unknown/unavailable/available/not_applicable is distinct from startup observation unknown/detected/not_detected/not_applicable. Detection requires available monitoring plus evidence; unavailable cannot mean “not detected.” Preserve opaque fields; max 64 KiB per fact payload.

Clock correction, gain readback, digital amplitude/clipping, USB overflow and software drops do not establish calibration or ADC sensing. Current RTL/SDRplay providers do not expose ADC observations and report this explicitly; that does not assert the physical device can never support it. A frozen interval's overload remains unknown even if startup reported none. Dynamic/interval coverage requires a new protocol; repeated Ready calls are not telemetry.

Session generations propagate the same startup facts to compatible sharing leases and clear them on source change. Frozen capture retains facts/request/actual/opaque context and verifies save/readback. Calibration alone does not change dBFS to dBm; that requires applicable amplitude calibration/conversion evidence.

## Arbitration

Share one acquisition only if its complete band/rate satisfies every demand; exact monitoring config and minimum broadcast demands differ. Check actual readback again. A single tuner's incompatible request reports conflict and preserves existing tasks; retuning requires explicit action and must not silently interrupt recording.

Stop monitoring releases only its lease. Remaining playback can choose a smaller legal configuration with reconfiguration/buffering visible, without promising gapless retuning. Stop playback releases itself; hidden panels can keep playing. Disconnect/exit releases all live users, but does not stop hardware-independent file playback/replay.

When only minimum consumers exist, compatible monitoring may upgrade the source configuration, satisfying all demands. An existing exact-config consumer pins it. Economic reconfiguration after release uses the union of all remaining bands, not only the first consumer.

Auto mode uses receiver recommendations; manual mode uses the selected broadcast frequency with current manual settings and full hardware validation. Both retain request/readback and preserve wideband preferences. Identity uses actual device equivalence: index/serial aliases must not double-open, nor block another free device.

Only after UI applies this generation's configuration does the host release I/Q to that demodulator. Skipped input while waiting is `configuration_wait_pairs_total`. Lease release does not wait for an already-entered nonblocking callback; shared worker state must outlive it independently of destroyed widgets.

## Status bar

Persist receiver/tasks/host load/actionable warning and clickable detail navigation; narrow layouts prioritize task/failure with tooltips for others. Detailed status remains available. Separate driver continuity, each software analysis stage and audio underrun. Historical totals are not current failures. Update notifications do not overwrite persistent runtime summaries. Use resource text/fallback.

A stopped monitor cannot fabricate final driver counters while another lease still runs; mark unfinished and acquisition generation. Preserve structured driver failures. Monitor queue rejection is consumer software loss, not hardware loss.

## Acceptance

Independent receiver/research providers verify operation without new host-ID branches. Cover standalone listening, sharing/conflict, stopping monitoring while playing, last release, bad actual readback, failure isolation, rapid restart and sustained actual-byte budgets. Check favorite naming/deletion/restart, event favorites, search cancellation, DTMB same-multiplex switching and existing manual/monitor/record/replay/layout behavior. Report synthetic/physical/external conditions separately; antenna-free uConsole checks establish build/start/modules only.
