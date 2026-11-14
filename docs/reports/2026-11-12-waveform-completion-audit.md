# Waveform laboratory completion audit

English · [简体中文](2026-11-12-波形实验室完成条件审计.md)

This is the audit before trained PSK/OFDM reception and Intel Mac acceptance. Subsequent [PSK/OFDM acceptance](2026-11-12-psk-ofdm-acceptance.md) and [native Mac deployment](2026-11-12-native-macos-deployment.md) supersede its earlier missing-EVM/environment-blocked conclusions. Preserve its baseline and failure history; do not read the old platform state as current.

The audit follows specifications22–24 and repository constraints, separating functionality, numerical/contracts, actual UI and distribution evidence. Windows/Linux had native workflows, real packages and sustained physical-input evidence. At this audit date the user had no Mac, so complete supported-platform acceptance remained externally blocked. An initial Windows native-export timeout had not reproduced or been diagnosed.

## Evidence baseline

`f578866` was the last production behavior change in this batch. Later additions covered hardware acceptance, explicit test language and export diagnostics. `1770164` identified actual packages/Linux 101-test regression; `d39c862` Windows 99-test regression; `e7fae8d` strengthened real file-dialog acceptance with focused builds, offscreen and three native layouts. Linux 98/101 passed with3 SDRplay external skips; Windows 98/99 passed with1incompatible-default-audio skip,0failures.

Private roots: `/home/vicliu/.cache/amc-waveform-01a1c9fc` and `C:/Users/vicliu/.cache/amc-waveform-01a1c9fc-windows`. Private I/Q remained outside Git. Full chronology is in the [implementation record](2026-11-11-waveform-implementation.md).

## Completion evidence

| Requirement | Implementation/checks | Audit boundary |
|---|---|---|
| P0-A arbitrary recording windows, segments, mathematical traces/validity |`signal_window`, `waveform`; independent continuous-time truth, three codecs, coordinates over2^53, short reads/gaps, low amplitude/cancellation |Numerical scope verified; no modulation/carrier claim |
| P0-B file/event entry, shared range/zoom/cursors, measurements/IQ plane |View/laboratory contracts; actual pixel aggregation, single-sample/large-integer cursors, invalid-line breaks, changed-source rejection and reference retention |Verified; failed load retains prior result |
| Save/reopen and whole-directory migration |Save worker, actual Qt file dialogs, QSaveFile, captured-copy digest/readback, model/recovery workflows |Configuration commit and sample-archive success remain separate states |
| P0-C nonblocking ingress and byte/time/segment budgets |Capture/source contracts, FIFO freeze/generation isolation, two180sactual Qt captures, stop/resume/conflict and digests |Bounded behavior/rejection verified; actual rejects preclude lossless claim |
| Reception demand and provider recommendation/validation/readback |`ReceptionDemand`/`ReceiverSession`, independent provider; shared2 MS/s→low-rate independent input→restore |Existing protocol verified; runtime calibration/ADC telemetry unknown |
| P1 frequency translation/FIR/rational resampling/support/source clocks |Processing/recorded-channel contracts; independent gain/frequency/delay/rational-clock/gap truth, long blocked files/budgets |Declared offline method, without borrowing unproved live-Channelizer capability |
| P1 STFT/PSD/correlation/histogram/power/units |Independent integral/window-energy/two-tone/complex-correlation/sparse mapping/budget/cancel truth; native region/apply/reopen |Normalized dBFS/Hz verified; no dBm calibration |
| Long-file on-demand processing and location beyond preview |Recorded worker; real434/428 MHz recordings; separate long range, save/reopen and16,384-point original neighborhood |File processing verified; noise/new capture is not truth for an earlier signal |
| P2 provider parameters/generation/alignment/fitting |Model/parameter/builtin/digital contracts; eight models and independent provider; incorrect models/ambiguity |Explicit conditions, without automatic modulation recognition |
| Generic model UI, original/processed input, observation/reference/residual, v6 |Actual native wide/scaled/compact workflows; oldv1/v2evaluation andv6method compatibility |At this batch no PSK/OFDM symbol EVM without sync/equalization |
| P3 generic evidence/event/input/stages/DAG |Independent digital/measurement providers, unknown fields and bounded traceability; exact original/rational location |Nondigital evidence preserved without forced Hex |
| Symbols/bits/bytes/frames/unknowns/checks |Independent continuous-time FSK and GF(2)CRC; independent LoRa TX encoding/IQ; errors/erasures/truncation/missing-check/candidate negatives |Explicit recovery conditions; I/Q storage bytes are not payload |
| Export, source protection andv7save/reopen |Complete JSON/known-byte subsets, invalid-frame candidate bytes, unknown-byte rejection, independent resources; actual writes |Dialog acceptance, worker commit and exported evidence checked separately |
| Latest433 MHz investigation |40 inputs/554,338,112 bytes rehashed; 39content/independent CRC/pair/complement-sum checks; 4095 independent transmitted codewords |36strict,3weak candidates,1unrecovered; no device/business attribution |
| Native layout/restart/panel recovery |Workspace extension/restart contracts; Windows/Linux 1600×1000,2000×1250,640×480 and actual-package dark layout |Two platforms verified; initial unreplicated export timeout retained |
| Sustained selected detectors |Actual production CLI 10 MS/s,8 MHz,180s; LoRa/FSK/OOK/ASK/FM; 1.8billionpairs with stage conservation |This run's software accounting; no24h/hardware-losslessness claim |
| Actual Unicode archives/modules/toolboxes |Linux `package-1794468042463939974`;Windows `package-1794467792229915900`; transactions/native start/extracted suite |Windows/Linux verified; Mac blocked at that time |

## Architecture audit

Codec/sample facts belong to `backend/iq`; detector semantics to detectors; model/recovery algorithms, parameters and resources to providers; declared registries/live_registry perform assembly. Module-dependency and extension contracts passed. Independent receivers, containers, models, digital/measurement recovery and multi-panel providers entered unchanged stable consumers.

Pending whole configurations drive descriptions/dependencies/validation; existing invalid values remain visible without clipping. Hardware conflicts/readback use the independent reception protocol. Provider resources ship with their provider; French/Dutch additions and per-key English fallback were exercised. Tests select their assertion language explicitly, without changing the product's system-language behavior.

Codecs own byte/normalization/full-scale semantics; original encoding and frozen CF32 encoding are distinct. Unknown event/method/resource fields and independent frame names are retained. Older analysisv1–v7and provider-call semantics have explicit compatibility checks. Protocol upgrades express new capabilities rather than pretending old versions already support them.

Distribution uses existing catalog/transactions; test signing stays separate from release keys. There was no formal release/deployment in this audit batch. Generic workspace descriptors register View entries and preserve layouts; ordinary resizing does not overwrite them. Concrete parameters stay in their research module.

Resource budgets account actual payload, retained time and stage-specific queues. Freeze does not stop other leases. Status does not wait for unrelated DSP. Paths remain native and cross Qt/JSON/CLI through common UTF-8 conversion. Actual Chinese/non-BMP package paths covered application/modules/toolboxes on Windows/Linux; Mac was not yet verified.

## Unproved or limited at this audit date

- Mac native UI/Unicode packages required an external environment; cross-compilation or older releases could not substitute. The later Mac report now resolves the Intel part.
- Initial Windows 1.25-scale native-export timeout remained unexplained. One-off, three-layout and ten-repeat checks later passed. Better dialog/commit diagnostics were test evidence, without a proven product fix.
- The new428 MHz check used an existing centered recapture with no detector event. Its one-second spectrogram was not modulation truth or reproduction of the earlier filter-edge signal.
- Generic unknown private-protocol recovery, LoRaWAN/vendor/business identity, controlled-transmitter truth, runtime RF calibration/ADC telemetry and long-term losslessness were not promised. Automatic PSK/OFDM synchronization/equalization/EVM was also outside this earlier batch, then implemented and accepted separately.
