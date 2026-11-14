# Waveform laboratory and traceable analysis

English · [简体中文](22-波形实验室与可追溯分析实施规格.md)

This specification implements the 2026-11-11 workspace and P0→P3 priorities. Delivery is real Qt behavior, shared computation, provider protocols/resources and evidence, not a prototype/static image. Preserve recording/detection/broadcast/workspace/user preferences and follow specifications 16–18/21.

Recorded, frozen-live or event input supplies an explicit I/Q interval for linked observation, measurement, hypotheses and evidence. Unknown modulation is still researchable; no detector-ID whitelist may block it. The current implementation includes all stages below plus trained PSK/OFDM (25) and Intel Mac qualification (26); historical deferrals are not current exclusions.

## Ownership and numerical invariants

Backend/iq owns versioned segmented windows/sources/clocks; backend/dsp owns mathematical tracks, processing/comparison and generic result contracts. LiveResearchProvider/receiver leases/Workspace own lifecycle. Model/recovery providers own methods, descriptions, whole-parameter validation and resources; registries are assembly. Generic math is independent of Qt/drivers/sessions; main window imports no built-in research implementation and routes no model/detector IDs.

LiveResearchProvider v3 retains v1/v2 layout/semantics and appends extra panels and `recordingActions(ResearchRecordingContext)`. Context has native path, optional file-relative sample range and unscreened event JSON. Actions may show declared provider panels. Tool-to-main-panel maps own diagnostics; extra panels are not extra I/Q subscriptions. View/persistence are generic; hide is not stop. External snapshot IPC gains no implicit live/native capability. Independent multi-panel/recording providers verify compatibility and failure isolation.

- Integer source indices; display time is integer difference divided by actual rate. Separate file, stream and local coordinates; no invented UTC.
- Shared codecs own decoding/bytes/normalization/full scale. Native paths use the shared UTF-8 boundary.
- Multiple continuous segments; gaps are not zeros or connecting lines, nor crossed by phase-difference/unwrap/smoothing. Unknown-length gaps remain boundaries.
- Low amplitude invalidates phase/frequency; first sample has no adjacent difference. Label wrapped/unwrapped and raw/smoothed estimates/parameters separately.
- Full-resolution data precedes display aggregation; preserve extrema/gaps/invalid regions and exact cursor/measurement values.
- Adjacent phase difference is discrete complex-baseband frequency with reference/alias/low-amplitude/multi-signal limits. Computable noise phase does not prove a carrier/SNR. Raw I/Q plane is not synchronized constellation.
- Every result retains source/range/method/version/units/parameters/validity/history. Model fitting freedoms are explicit; residual does not establish modulation/fault/EVM.

## Delivery order and acceptance

| Stage | Deliverable | Evidence |
|---|---|---|
| P0-A | Native segmented windows, arbitrary origins, I/Q/amplitude/phase/difference/frequency/validity/history | Independent formulas, three encodings, low amplitudes, wraps/gaps/chunks/exact clocks/cancel |
| P0-B | Native file/range workspace, linked selection/zoom/cursors, A/B, complex plane, source details/save/reopen | Burst-to-sample interaction, aggregation invariance, no gap lines, independent slice match |
| P0-C | Nonblocking bounded capture/freeze/resume/switch/stop, rejected vs retention counters; provider-demand acquisition/recommend/validate/apply/readback/recapture | Sustained/short-read/budget/overload, same live/offline kernel, independent sharing/conflict/actual settings |
| P1 | Full-band time/frequency selection, channel extraction, translation/FIR/rational sampling, raw/processed, STFT/PSD/correlation/histograms/statistics | Independent periodic/two-tone/multi-signal truth, delays/clocks/units/gaps, bounded matrices and on-demand files |
| P2 | Provider models/parameters/validation, reference/alignment/error/fitting, including explicit PSK/OFDM | Truth/offsets/wrong models, independent provider/language fallback; no unqualified EVM |
| P3 | Event→samples→processing→features→conclusion, symbol/bit/byte/frame/Hex/ASCII, checks/location/export/reproduction | Unknown events, missing support, independent known frames and failure/truncation/check/provider cases |

Receiver/session controls remain reachable. Research and monitoring presets are explicit; View/docking/floating/tabs/restart and small/1.25-scale actual layouts are required. Completed models/evidence have real pages, not P2/P3 placeholders. Text belongs to resources; capture/preview/frozen/recording/working/stopped states differ.

## 433 MHz investigation requirements and evidence

Private `recordings/433-survey-20261111/` and hashes informed acceptance, not production script interfaces or public assets. Preserve source identity, original/derived clocks, rational ratios/filter delay and full processing chain. Power is dBFS, not uncalibrated dBm.

Research cases include SF11/BW500k near 433.012 MHz; a persistent narrow carrier/periodic frequency swing near 434.7246 MHz repeatedly labelled OOK/ASK; edge-filter chirps near 428.8068 MHz. Treat these as measured candidates, not multiple devices/packets or confirmed purposes.

Digital recovery must show symbols/bits/bytes/frames/checks and sample mapping; raw file bytes are not air payload. Algorithm/provider owns sync/demapping/deinterleaving/FEC/dewhitening/framing. Candidate, sync failure, check passed/failed/absent/not-applicable/unknown, truncation and possible encryption remain separate. Information can instead be analog audio/measurement/unsupported; no forced Hex from a weak carrier or tentative ~67 Hz swing.

Historical refinement: initial 50-byte CR4/5 explicit-head/no-PHY-CRC candidates had 12–19 parity errors per 99 codewords; finite timing search removed payload parity but left head-block byte ambiguity. Later `protocol-profile/verified/decoded` assets resolved 15 frames using outer CRC16, 48 encoded bytes→24 decoded, inner sum, including four held-out frames. Fixed-length init/xorout is not uniquely identifiable; no named vendor protocol follows.

Follow-up assets contain 36 strict contents, three weak candidates and one unrecovered event. Weak cases included unchanged payload with one parity error and two invalid pairs repaired under unique CRC/inner-sum constraints. Do not call 39 contents 39 strict frames. E32/CDEBYTE-compatible encapsulation remains a hypothesis, not manufacturer/use proof. Fields are explicit hypotheses: opaque six-byte value (not MAC/OUI), one-byte sequence, LE24 minute counter, BE16 seconds; epoch/units/use are unknown. Preserve passive user observation: no actively operated transmitter/control trigger, no neighbor/property attribution and no exclusion of unattended owned devices.

Production providers subsequently reproduced all 40 events (36 strict/3 candidate/1 unrecovered), matching 39 contents and independent checks. Do not copy the script's repair history: two weak cases in the production processing directly reached zero codeword distance with qualification gates disabled and remain candidates. Details belong to specification 24/dated reports.

Research changes acquisition needs; 433 MHz/10 MS/s/current gain are not product limits. Provider declares band/guards/rate/gain demands, receiver owns legal suggestions, session evaluates all users, then applies/readbacks/recaptures explicitly. No silent retuning. Save requests/actual/calibration/overload; digital scaling does not repair analog clipping. Historical authorized passive hardware checks verified no concurrent client then; this does not imply perpetual availability.

## Sources and frozen containers

SignalSourceProvider v1 assembles in `backend/iq/signal_source.cpp`: suffix/resources/native path/stored count/original stream/reader and explicit contiguous stored→source mappings. Original vs storage encoding differs; normalized CF32 is not normalized again as old integer input. Validate declared segments/count/capacity; I/O chunks are not source boundaries.

Frozen `org.amc.signal-window` v1 stores `capture.amc-window.json` + codec-written `samples.cf32`. Preserve UUID/original encoding/actual stream/original clock/display origin/segments/processing/reception context, decimal-string positions/counts. Unknown absolute arrival is null; saved time is only save time. Segmented input is not falsely continuous SigMF. Exclusively create new data directory, finish samples, rename temporary manifest; failure cleans only its own fixed files and preserves unknown/old files.

Config v1 references remain readable; v2 explicitly adds analysis-directory-relative paths using `path_to_utf8(..., PathTextFormat::generic)`. Move analysis and adjacent capture together to reopen; missing/changed data is still an error, not permanent-availability assurance.

## Channel processing, mapping and on-demand files

ProcessedSignal v1 retains original immutable window, output rate and exact integer-plus-reduced-fraction clock mapping. AnalysisTrace v2 uses its declared clock; raw consumers retain integer source clocks. ProcessedSignal v2 accepts an independent file/range instead of a resident raw window; AnalysisTrace v3 supports that source. A preview is not the entire observation. Cached results can remain viewable after source loss, but future reads/reproduction revalidate availability/identity.

AnalysisTrace v4 adds optional compact subset mapping (null retains full parent axis), same-clock integer positions and compact value indexes pointing only to observed parent points. Do not erase known/unknown gaps. Pair planes need identical maps; missing computations are blank. Count shared map capacity once. Statistics use the feature's own map, not parent indexes into shorter arrays.

File extraction reads central blocks plus bilateral filter support through source protocols, with a fixed rational output grid/mixing origin. Write each preplanned position once; missing/duplicate points, changed history, budget/cancel cannot publish partial results. Bound reads/coefficients/temporary outputs/mapping/input/work separately: default center 8192 samples, read payload 1 MiB, workspace 4 MiB, mapping 8 MiB; input/actual overlapped reads ≤100M/256M positions, plus output/work caps. This is bounded range processing, not unbounded whole-file computation.

`org.amc.normalized-recorded-range` v1 digests versioned stream/origin/selected file range/segments/gaps/history then all normalized CF32 samples once in file order. Separate metadata SHA; exclude path and duplicate overlap. It is neither preview-only identity nor whole-file hash. Verify overlap bitwise against frozen preview and prior complete identity, preventing mixed old/new observations. Reserve file workspace/mapping in joint budget.

Analysis v5/channel v2 retains independent recorded-input range/full identity and separate raw preview. Preserve selections beyond preview rather than clipping or calling unloaded data missing. Locate processed B by loading ≤16384 original neighbors, reapply the saved chain/identity, retain fractional selection/cursors without applying drafts. Rate changes remap exact source time; distinguish nearby original observations from interpolation. Relocated analysis/source must reopen in a fresh instance without old paths.

Offline translation uses full offset/passband/per-side transition/output-rate validation and centered Blackman-sinc polyphase FIR. Emit only complete observed support; gaps stop support, ordinary chunks do not. Record equivalent causal delay, timestamp compensation and edge discard. Lazy phase cache, output and operation budgets avoid unbounded coprime-ratio tables. Existing real-time FFT Channelizer behavior remains separate; do not pretend it already offers these exact offline semantics.

Provider owns selected-channel controls/views. Raw/processed selections map both ways; an original cursor marks the preceding real sample, not an interpolated observation. Analysis v3 stores source/digest/method/applied parameters/independent views; frozen v1 still stores only original integer-clock data.

## Joint workers, window analysis and save

Joint payload default is 128 MiB across source/processed samples/traces/coefficients and still-live results; old UI result can coexist with one new result. One running job + latest pending request serves raw/channel/window/model/recovery, not multiple generation-retaining pipelines. Window matrix/workspace defaults 16/4 MiB; heatmap aggregate cache/workspace another 4 MiB; generic trace ≤65536 points then pixel aggregation. These are payload limits, not exact process RSS.

Analysis v4 records window method/actual clock/applied window/FFT/hop/units/statistics/exact interval/color scale. Selection refresh coalesces 100 ms latest range; do not save falsely synchronized pending state. New source discards stale refresh. Reopen first reproduces saved independent range. Time-frequency selection only edits channel draft/raw time, not implicit DSP/hardware changes.

Spectral kernel values are linear; display dB uses power reference one. Two-sided complex PSD is normalized power/Hz and integrates by bin spacing; per-bin power has separately summable normalization. Preserve window energy/coherent gain/ENBW/frame support/averaging. No dBm without calibration. Correlation keeps DC, never crosses gaps, normalizes actual pairs; invalid phase/frequency does not enter histogram/statistics.

Save worker holds immutable samples and applied/view snapshot, writes then rereads normalized digest and atomically commits QSaveFile config. Following new input pauses while capture continues; no new concurrent analysis, at most one later freeze waits for display. Cancel never joins on UI thread. If samples succeed but config fails/cancels, report their paths/states separately.

SignalCapture owns generic bounded encoded→segmented-window capture below Qt/sessions. Callback uses try-lock, actual capacity, block-size/count/byte bounds; worker decodes. Retention bounds normalized bytes, clock span and segment count, including short reads. Freeze is a FIFO boundary of accepted input; later input continues without contaminating the immutable snapshot. Stop/source-switch discards/counts pending old work and cannot publish it into a new generation. Reception, processing, persistence and UI need joint acceptance beyond the kernel.

## Validation and current state

Run module dependencies, sample codecs, extension architecture/behavior, live extension, workspace layout and affected recording/LoRa/FM/DTMB/CLI/GUI regression. Independent providers change actual extension inputs rather than mirror implementation. Separate offscreen/numerical, native desktop, true Unicode/non-BMP archives/toolboxes, physical input and deployment. Daily CI stays self-hosted; formal publication/private keys need their established scope.

The initial reference-only PSK/OFDM and absent-Mac stages are historical. Trained receivers/complex evidence/analysis v8 have Windows/Linux review evidence; subsequent Intel Mac native/package/deployment evidence is complete for its stated target. Reports retain first Windows export timeout and external skips. Physical capture includes actual rejections/gaps/retention; calibration/ADC interval telemetry remains unknown. No general lossless or arbitrary-air-interface claim follows. See [implementation](reports/2026-11-11-waveform-implementation.md), [receiver acceptance](reports/2026-11-12-psk-ofdm-acceptance.md), [Mac acceptance](reports/2026-11-12-native-macos-deployment.md).
