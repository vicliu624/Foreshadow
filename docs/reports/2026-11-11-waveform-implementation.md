# Waveform laboratory implementation and acceptance record

English · [简体中文](2026-11-11-波形实验室实施记录.md)

This is the consolidated11–12 November 2026P0–P3implementation chronology. Later [trained PSK/OFDM](2026-11-12-psk-ofdm-acceptance.md) and [Intel Mac acceptance](2026-11-12-native-macos-deployment.md) supersede this batch's earlier missing-EVM/Mac-blocked scope. Historical failures and source identities remain. The [completion audit](2026-11-12-waveform-completion-audit.md) maps final conditions to evidence.

## P0: windows, traces and source/capture protocols

The implementation added segmented immutable source windows and typed clocks/validity, codec-derived normalized access, waveform analysis, bounded recorded-input workers, common view/measurement controls, generic laboratory panels and versioned configuration persistence. Sources retain acquisition/storage encoding distinctions, explicit known/unknown gaps, coordinates over2^53and nearUINT64_MAX, and raw versus filtered traces. Invalid phase/frequency regions remain unobserved rather than joined or padded.

Capture uses actual allocated payload/block/fragment budgets, try-lock ingress, background decoding, FIFO freeze of an accepted prefix and source-generation isolation. Freeze remains immutable while other users continue. Rejection, processing error, canceled cleanup, normal expiry and source discontinuity have separate counts. Source/stream/reception contracts stay below sessions; research providers do not import orchestration.

`LiveResearchProvider`v3adds compatible multipanel/recording/action support to priorv1/v2. Independently registered providers demonstrate Viewentry/layout/lifecycle/unknown-field behavior. File/container providers demonstrate suffix/resources/source mapping without IDbranches. Save workers preserve nativepaths, protect original metadata/data, copy/readback-digest captures and distinguish sample-archive completion from atomicconfiguration failure/cancellation.

Isolated Linux source/build/deps/logs reused verified pinned JSON/KissFFT/DTMB/FFmpeg; privateNASMdid not modify system installs or relaxTLS. Initial mathematical/view/codec/architecture checks were followed by real Qt file-dialog save/reopen, independent language/fallback, unknown-event and changed-source/error-retains-old-result checks.

| Integration milestone | Full Linux suite | Scope |
|---|---|---|
|Merged capture/source baseline|77/80 passed,3 SDRplay environment skips|80.62s; affected12 checks then passed|
|Actual desktop capture integration|79/82,3 skips|73.95s; simulated full demand/readback/freeze/release/conflict workflows|
|First channel extraction|82/85,3 skips|73.37s|
|Window research,`8af7970`|84/87,3 skips|74.38s|
|Recorded-range chain,`009818d`|85/88,3 skips|75.17s|
|Native restart/layout correction|86/89,3 skips|74.12s|
|Startup calibration/overload distinction|87/90,3 skips|75.18s|
|Built-in reference providers|90/93,3 skips|74.54s, before complete modelUI|
|ModelUI/v6|91/94,3 skips|75.29s|
|Digital references/exact delay|92/95,3 skips|71.71s|
|Recovery kernel|93/96,3 skips|71.40s|
|RecoveryUI/export/v7|94/97,3 skips|72.35s|
|Paired-code application frames|98/101,3 skips|72.58s|

These counts identify successive snapshots, not a claim that every later edit reran every suite. Three real-driver/mocked-discovery conflicts were never counted as physical hardware passes.

## P1: channel processing, window research and long recordings

The separate offline channel method performs precise frequency shift, FIR low-pass and rational resampling with complete support, explicit delay/valid edges/rational source mapping. It is not an assertion that the older real-time channelizer had these offline semantics. Tests use independent frequency/gain/delay/clock/gap truth and actual capacity/budget negatives. Original preview and independently read file ranges can differ; whole-range incremental digests detect mutations outside the preview.

STFT/PSD uses explicit window energy, complete frames and normalized dBFS/Hz; correlation and frequency histograms preserve their input conditions. Independent power integration, two-tone/complex correlation, sparse clock mapping, cancel/budget tests and actual time-frequency selection/draft/apply/reopen passed. Fewer-than-one-frame input keeps valid time statistics while spectra remain blank. Initial screenshot sizing was incorrectly overwritten by workspace restoration; capture sizing moved after restoration without changing normal layout behavior.

Native XCB tests exposed restart/layout issues and verified automatic independent panels, visibility/floating/new-windowrestore/explicitreset, wide/scaled/640×480flows. Saved layout is restored once and ordinary resizing does not reapply presets. Startup ADCnot-overloaded and unknownselected-intervaltelemetry remain distinct; no RF calibration is inferred.

Actual434 MHz recording: 16million CI16 pairs,2 MS/s,434.7 MHz, SHA-256 `82d026b4a8c98cffd6615d41caffad4cc1ed96e86b7653e6be1733752a67ddc4`. A4096pointpreview coexisted with8million-pointfileprocessing at+24600Hz/fullpassband1 kHz/transitions1 kHz/output4 kS/s: 15976valid outputs,12001 taps,6000source-samplecausal-equivalentdelay compensated; 19,710,608support reads/191,727,976MACs. File hashes remained unchanged; this was offline work, not live throughput.

Later428 MHz workflow used an existing centered recapture,24million CI16 pairs/96millionbytes/2 MS/s/428.5 MHz, hash `ef603c1d82666a5454eac5673098f643c43359735abf7112dfe9bee0daec96cd`. First2millionpoints at+306800Hz/500 kHz passband/100 kHz transitions/output1 MS/s yielded999940 points,121 taps/60sampledelaycompensated.976complete FFT 1024/hop1024Hannframes, save/reopen and original-neighborhoo dB=1375906 passed on XCB 1.25scale. This no-event recapture was not truth for the earlier filter-edge signal or a modulation identification.

## P2: reference providers and generic comparison

Providers own tone/AM/FM/FSK/chirp/LoRa/PSK/OFDM generation, complete pending parameter descriptions/dependencies/validation and resources. Shared comparison fits declared complexgain/DC/delay, preserves ties/searchscope and exact source mapping, and treats generatedreference separately from observation. Independent providers/resources exercise unchanged consumers. Budgets account simultaneously live source/waveform/channel/window/model payloads and declared workspace within128 MiB, without permanently charging expired sequential scratch space.

ModelUIapplies to original/processed current views, retains reference time origin through zoom, shows I/Qoverlay/residual/complexplane, saves only applied parameters and preserves old results on failure/cancel. v6reopen retains methods/limits/input clocks; old provider-call and comparison-method versions remain compatible. An initial resource test omitted parent-directory setup and failed; corrected fixture preparation passed. Compact descriptions moved to scrollable parameter space, with actual640×480and≥90pxfirst-trace assertions.

Explicit PSK rectangular/RRC and OFDM references checked pulse/CP/subcarrier/training bindings, no clipping of invalid existingCPwhen FFT size shrinks, and exact integer delay (+3 PSK/+2 OFDM) errors below5×10⁻⁸. At this stage it was reference comparison **without automatic receive synchronization/equalization/EVM**; specification 25and the later report implement that subsequent scope. Small residuals were not labelledEVMoridentification.

## P3: recovery providers, evidence and export

Recoveryprotocolv1introduces typed digital/measurement/unsupported results, stages, symbols/bits/bytes, frames/checks and multilevel dependencies. Common validation rejects cycles, illegal values, support escapes or validated frames without completed required checks/scope. Unknown values retain reasons; exact disjoint support is a union, without filling gaps. OriginalFSKtruth uses independent continuous-time tones and GF(2)CRC, not receiver roundtrip alone.

Generic recovery workers/panels expose Hex/AS CII, frames/checks, values, stage/event details and original/rational input location. v7outeranalysis/recoveryconfigv1save applied parameters/methods/limits/ranges/clocks/page/frame/sequence selections and recompute on reopen. Independent digital/measurement providers and French-resourceincrements enter unchanged UI/export; nondigital results retain fields without forcedHex.

Evidencev1streams with32 KiB buffers,64 MiB wire/8 MiB source-envelope bounds. It preserves unknown fields, full values/support/dependencies, frames/checkstates/scope, decimal-stringlargeintegers and nullunknowns, without cloning a second complete JSON tree. Binaryexport requires selected complete knownbytes; knowncandidatebytes from a failed frame do not validate that frame. QSaveFile/fileworkerhandle atomic commits, cancel/write errors and original protection. Evidence-onlyexport does not archiveI/Qorhide ephemeral-source lifetime.

LoRa providercodec retains received codewords/masks/corrections/nibbles/bytes separately, diagonal deinterleaving, headerchecks/dewhitening/PHY CRC. CR4/5–4/6detect complete single-biterrors; CR4/7–4/8correct only unique candidates within2e+s<dmin. Missing CRC, parityfailure, erased/truncated input and repaired data retain candidate/invalid qualifications. I/Qdemodulation and bounded hypotheses feed the same evidence; no algorithm-IDhost branches were added.

The paired-code empirical family adds received CRC16/complement-sum checks and bounded repair without manufacturer/business identity. Actual private433 MHz batch finally contained40 inputs/554,338,112 bytes: 36strict,3weak candidates,1unrecovered.39content/independent CRC/pair/sumresults and4095 independent transmitted codewords were checked; later audit rehashed inputs without claiming to rerun decoding. Independent frame labels replaced generic fallback names; saved state/export retains provider resources.

## Actual packages, Windows fixes and sustained hardware

Linux`e93f9f9`actual packaging initially rejected a zero-byte FFmpeg source archive. The verified18,170,864bytepinned archive, hash `e3976ef0636e9c12d68c193abc4898ab50f05be3a4996026abc60c18be7447d4`, repaired the private cache without weakening checks. Application/two receivers/external research transactions and Unicode extracteddesktop checks passed. Later`d0ee1f4`repeated native Unicode startup and archive tests; these were acceptance artifacts without formal signing/publication.

Windows used private Qt 6.8.3/MSVC 19.44/SDK 10.0.26100/vcpkgpinned`177e8175121536d414f405ed8b3099b325a42638`. A privatechainload/toolchain and libusboverlay resolved compiler discovery without system registration changes. Real FFmpeg build exposed uppercaseenvironmentnames, localized MSVC identification and UTF-8resource metadata. Exact-context build-tree configure/resource-wrapper corrections preserved pinned source/archive/codecchecks; VSLANGalone was insufficient on a Chinese-only compiler. `f578866`completed the382.205sactual whole build.

`1770164`made a language-dependent assessment test explicitly selectEnglish, without changing product language. Windows 99 checks: 98 passed/oneincompatibleaudio skip/0failures,80.88s;`d39c862`diagnostic-onlyrerun matched,80.42s. Actual Windows package transactions/Unicode native startup and extractedstagewith package-onlyPATHpassed104.349s. Application package SHA-256 `d54ed4ae50bc48caeb72a64250628b0adc128512edf470e897f92cd3ceb99871`. Linux final101checks98 passed/3environment skips; real archive passed82.499s, application SHA-256 `54314731e30c1729e7a313fdde23861af0dff1d34bb9822005b0fa10bb0e877e`.

Two actual Qt 180scapture runs exercised shared2 MS/s→provider-recommendedindependent input→restoredsharing,30sfreeze, other-lease release/resume,120ssave/reopen, stop/retain and another user continuing5s. A1,048,576sampledigest matched. First run had16384try-lockrejects; final tool`cc35781`had98304/81920rejects in first/thirdgenerations, noqueuefull/processingrejects, exactknown-gap accounting and drainedaccepted/processed/canceled balances. This qualifies bounded behavior, **not lossless capture**. Retention stayed8 MiB; generationqueuepeaks262144/65536/131072 bytes.

A separate production CLI 180s/10 MS/s/8 MHz/all LoRa/FSK/OOK/ASK/FM runtotalled1.8billionpairs in source/main/narrow/recording-pathposition, zero known analysis rejection/gaps/skips; queuepeaks2,424,832/1,310,720within16 MiB each. Recording-pathconsumption is not fullcontinuousdiskretention. No hardware counter/calibration/decoderaccuracy or24hguarantee follows.

First Windows 1.25scalenativebyteexport timed out. Later focused/three-layout/ten-repeatflows passed, but the original cause remained unproved. `e7fae8d`corrected a definite test diagnostic defect: file-dialogchoice now asserts actualAccepted/nonempty/not-timed-out before waiting for backgroundcommit. Productexportbehavior was unchanged, and this did not establish the original timeout's cause. Linux/Windows focusedoffscreen/nativewide/scaled/compactchecks passed; no fictional whole-suite/package rerun was attributed to this test-only increment.

At this batch, Mac was externally unavailable under the user's decision; later Intel Mac acceptance resolved that earlier condition. Calibration/intervalADCtelemetry, private-protocol identity and long hardware continuity remained unknown. Full private logs/source snapshots/native images/hardware counters/package results are under `/home/vicliu/.cache/amc-waveform-01a1c9fc` and `C:/Users/vicliu/.cache/amc-waveform-01a1c9fc-windows`. Original Chinese chronology retains individual incremental log names and failed attempts; current normative behavior is in specifications22–26.
