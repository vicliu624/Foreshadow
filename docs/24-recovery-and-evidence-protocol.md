# Recovery and traceable evidence protocol

English · [简体中文](24-信号恢复与证据协议规格.md)

Laboratory P3 connects events, input samples, processing, symbols/bits/bytes, frame checks and conclusions through real browsing/location/save/reopen/export. Provider/investigation/platform stages are dated separately; later PSK/OFDM v2 evidence supersedes initial integration exclusions.

## Ownership, versions and capabilities

RecoveryProvider owns full descriptions/validation, synchronization/decision/mapping/FEC/dewhitening/framing, quality thresholds/checks/resources. Assembly is `plugins/research/recovery/registry.cpp`. `recover_signal`, validators, workers/UI/export consume protocols, not concrete receivers or modulation/tool-ID branches. This is a static versioned C++ contract, not a runtime ABI. Unsupported provider/method/result capability is explicit; preserve opaque event/method data.

v2 retains v1 digital/measurement/export behavior, adds complex sequences, domain resource key, generated-reference flag and per-point group/signed coordinate. Measurement can carry complex, not digital sequences/frames. Require finite values; unknown points retain reason. Generated reference depends on actual measured sequences, not an observation leaf. RecoveryMetric has ID/title/unit/availability/reason and renders generically. Count all new capacities/strings. Evidence v2 uses null for unknown complex value and preserves candidate/group/coordinate/support.

Recovery config v2 records provider protocol/method and complex-plane/group; outer analysis v8 declares it. Supported v1–v7 files remain readable; a lower version carrying undeclared new capability fails. Independent non-modulation v2 provider verifies display/fallback/export/reopen without consumer changes.

## Input, stages and dependency graph

Input is immutable WaveformAnalysis + exact selection under its actual clock. Codecs already normalized I/Q; recovery does not parse storage. Processed positions retain rational original mapping. Source identity/processing belongs to source/workers/config, while recovery retains the actual parent analysis/event. Reopen revalidates identity/full chain.

RecoveryData distinguishes digital, measurement and unsupported. Nondigital results cannot contain digital sequences/frames; Hex appears only after digital byte capability. Stages carry ID/resource title/upstream indexes/parameters/features/conclusions in topological order. Symbol/bit/byte/complex sequences declare stage/type; values keep known/value/stage metric/reason and either input support or earlier-sequence dependencies. A metric is not automatically confidence.

The same DAG represents aggregation/deinterleaving/FEC. Leaf support is required input and may contain unobserved gaps; do not imply every sample exists. `recovery_input_ranges` preserves 64-bit integers, traces a bounded unique-node graph, merges overlapping/adjacent support into exact unions without filling separated intervals, then uses parent mapping for exact original positions. Parent observations/continuity remain authoritative. Bound visits/pending/output, not unlimited dependency expansion.

## Frame and byte states

Unknown bit/byte storage is not recovered data; show `??` and never export invented zeros. Partial symbols/bytes are explicit even if eight slots exist. Frame states: candidate/validated/invalid/truncated. Check states: unknown/not-applicable/absent/passed/failed, with stage/required/evidence.

Validated requires a validation-scope resource key, at least one passed required check, all required checks passed, no failed checks and all relevant bytes known. Absent optional PHY CRC can coexist with passed application checks but cannot become “PHY CRC passed.” Check consistency proves no device identity/business meaning/encryption. FEC keeps received/known-mask/corrected sequences and dependencies; do not overwrite weak/error evidence into strict results.

## Budgets

Defaults: structured payload 8 MiB, algorithm workspace 1 MiB, input ≤1M clock positions, values 131072, dependencies 262144, frames 4096, stages 64, sequences 16, work 64M. Account actual vector/string capacities, not size alone. JSON separately bounds cumulative wire bytes 64 KiB/nodes 2048/depth 16, prechecking raw key/string totals. This is not an exact JSON allocator or process-heap measure.

Provider checks before allocating/computing; generic validator checks complete output. Cancel publishes no partial result. Kernel adds no threads/receiver lease; desktop reuses serial latest-pending work, retaining prior completion on error/cancel.

## Desktop, persistence and export

Fifth laboratory result page and generic parameter descriptions drive Hex/ASCII, frames/checks, symbols/bits/values and stage/event views. Tables are row models, not a widget per symbol. Nondigital results hide digital pages but retain opaque evidence.

RecordedWaveformWorker fixes input/range/method/parameters/event at apply; window/model updates can reuse immutable recovery, while replaced base input invalidates it. Joint budget counts live waveform/window/model/recovery capacities, recovery workspace and a separately described JSON wire allowance. Source-location actions use integer analysis clocks/rational original support. Loading original neighborhoods outside preview preserves applied processed recovery; separated support can have an overview spanning blanks but never manufactured data.

Analysis v7/recovery config v1 stores applied provider/method/parameters/opaque records/exact range/clock/limits/page-frame-sequence selection, not drafts. Reopen recomputes/validates source/chain/clock/version; no silent default. Frozen source uses existing sample archive/readback/atomic save and continues capture. v8 adds complex capability without downgrading older files.

`org.amc.recovery-evidence` v1 (v2 for complex) carries parent snapshot, recovery clock/range, method/params/event, stages, all values/support/dependencies, frames/fields/checks/scope. Sample positions/counts/recovery integer values are decimal strings; bounded sequence indexes remain integers. Unknown is `known=false,value=null`; raw stored candidate appears only as `candidate_value`. Preserve unknown fields.

Stream export through 32 KiB buffer without duplicating the whole DAG into JSON. Default wire cap 64 MiB plus separate 8 MiB parent envelope; propagate cancellation/sink failure. Binary export requires a nonempty all-known byte subset and keeps exact values. Known bytes of a failed frame may be exported as candidates, not reclassified valid.

One file worker uses immutable result/selection fixed before dialog, QSaveFile atomic commit, and source/meta overwrite protection. Failed/cancelled output is not published. Save/export completion states differ. JSON omits full I/Q and marks transient input unavailable after close; save-analysis plus sample archive supplies durable reopen. Keep JSON beside bytes because a raw byte file carries no validation semantics.

## Bounded explicit 2-FSK

`manual-2fsk-fixed-crc16` takes integer symbol grid, two frequencies, RMS/projection-separation gates, bit order and payload length. Two noncoherent complex projections per symbol; low power/ambiguous/missing/unknown continuity erases. Ordinary short reads preserve decisions; partial tail is incomplete.

Frequency 0/1 maps bit 0/1. Frame is fixed payload + two big-endian CRC bytes: polynomial 0x1021, init 0xFFFF, xorout 0, nonreflected; no FEC, ≤64 frames/selection and generic budgets. Passed CRC validates only these declared assumptions, not automatic capture/named protocol/hardware continuity/attribution.

Independent continuous-phase transmitter and GF(2) whole-polynomial division test raw/rational clocks/chunks/wrong bits/order/gaps/low power/truncation/limits/cancel. Independent paired real-axis provider verifies multilevel dependencies/absent checks; independent measurement provider rejects forced Hex. Native save/reopen/export evidence is separate from actual RF/package acceptance.

## Bounded LoRa I/Q recovery

`bounded-lora-phy` consumes actual-clock normalized input: 1/2/4/8/16 samples per chip, FFT ≤262144. Existing channel extraction can obtain a suitable rate. Declare SF/BW/data-symbol offset, optional preceding up-chirp reference, CFO and I/Q direction; arbitrary FFT peaks are not automatic LoRa identification.

Dechirp complete support, finite zero-padded transform, aliased power folding and local three-point peak interpolation. FFT zero-padding is not filling missing observations: gaps/unknown continuity/incomplete support stay unknown. RMS/folded concentration are provider gates, not calibrated power/SNR/probability. Missing reference keeps dependent symbols unknown.

Timing grid ≤9 positions; timing/drift/bin hypotheses ≤32768. Explicit-header check filters assumptions; ranking records PHY CRC/truncation/unknowns/head-payload distances. Merge identical content; retain ≤32 distinct candidates. Report discarded worse content vs incomplete best set. Conflicting best contents expose only agreed bytes; incomplete best set makes all payload unknown. Unique no-PHY-CRC content stays candidate; application CRC/pairing/sum does not enter this PHY provider.

Raw FFT observations, global hypotheses, aligned symbols and code stages enter the DAG. Byte support includes references/global search context, preserving unobserved time between them. Trace shared nodes once within limits. Admit FFT input/output/queried plan space/reference/folded spectra/candidate capacities/text/codec temporaries before allocation. Defaults evolved from 64 KiB/32M to 1 MiB/64M; old v7 smaller configured caps remain enforced. Work units bound FFT butterflies/mixing/folds/symbol/codeword tests, not exact FLOPs. A real 7575-hypothesis job used about 47.46M and correctly failed the old 32M cap.

Independent I/Q/CFO/conjugation/chunks/drift/support and public SF12 UI save/reopen are verified. A 10 MS/s investigation range was FIR-extracted to 1 MS/s and recovered a 50-byte SF11/BW500k zero-distance/no-PHY-CRC candidate, independently matching outer CRC/pairs/inner sum. This was a slice-stage result before the next provider, not device attribution.

## Paired-encoding CRC16 empirical frame family

`bounded-lora-paired-crc16` is separately registered, reusing internal PHY descriptors/finite demodulation/codec. It owns pairing/CRC/ranking/repair/fields/resources without changing generic UI/workers/config/export or PHY validation semantics.

Declare even encoded body 2–248 bytes (default 48), 01/10 polarity, reflected CRC working init 0x0399/final xor 0 by default, then two big-endian CRC bytes. Polynomial normal 0x1021/reflected 0x8408; decode MSB-first, requiring decoded byte sum mod256=0. Fixed-length samples do not identify unique init/xorout/fixed prefix or a named vendor protocol.

Application-valid candidates rank within finite PHY search, still retaining codeword distances/truncation/unknowns/PHY CRC. At most 32 distinct PHY contents; truncated application-best retention cannot imply uniqueness. Preserve representative hypothesis, raw FFT/received/corrected codewords/PHY bytes/application choices/inner bytes/manual assumptions. Byte location includes full-frame constraints used to choose it.

Repair defaults off; when on, change only invalid/unknown pairs, never legal observed pairs or received CRC. Default erasures 12 (max16), ranked inputs24 (max32), assignments131072 (max1048576), distinct solutions16. CRC differences/Gray iteration reduce redundant work without evading attempts/work/workspace/result/cancel limits. Preserve original/inferred pairs and locations. Multiple solutions expose only consensus; incomplete retained solutions cannot export certain content. Truncated/unknown received CRC cannot manufacture completeness.

Validated application relation requires complete/unique/no repair, all physical observed distances zero, and enabled RMS/concentration gates. Repair/physical errors/unknowns/disabled gates remain candidates. Observed required failure is invalid; truncation stays separate. Absent PHY CRC remains absent, never implied passed by application checks.

Post-four-byte-prefix XOR, removing inner checksum, and optional 19-byte status layout are manual assumptions. Layout needs a complete 48-byte-body config: opaque six-byte value, sequence byte, LE24 minutes/BE16 seconds/unknown-unit fields without vendor/MAC/OUI/use/epoch claims. Provider owns whole-config dependencies/resources; save applied config and per-key fallback.

Independent sender codebook/interleaving/continuous-phase/GF(2) division covers known bytes, zero-distance/weak unchanged-payload cases, bounded repair, CRC/sum failures, config/resources/export/huge origins. Pure repair tests include multiple valid solutions/retention caps. Qt shows raw corruption vs repaired candidates, exports candidates and reopens v7 at native wide/scaled/compact.

On 2026-11-12 production reproduced 40 field events: 36 strict, three weak, one unresolved; 39 contents matched bytewise/independent CRC/sum. Weak gate-disabled configs remain candidates; two directly achieved zero codeword distance in this chain, so do not copy the old script's repair history. Independent sender checked 4095 head/payload codewords. Private data/evidence/config remain ignored; qualification is precisely the dated report's scope.

## LoRa symbol coding/FEC

`plugins/research/recovery/lora/codec` owns symbol mapping, diagonal deinterleave, codewords, header/check, nibble packing, whitening and PHY CRC, consuming decisions into generic RecoveryData without detector imports. Parameters: SF7–12, CR4/5–4/8, explicit/implicit header, LDRO and correction policy; implicit requires declared length/rate/CRC.

First block is eight symbols and SF−2 CR4/8 words; explicit header occupies five nibbles, remaining nibbles enter payload. Later width SF or SF−2. Whitening initial FF, polynomial x^8+x^6+x^5+x^4+1, payload only, not PHY CRC bytes. Preserve raw fourth header nibble, using only its low bit in the known five-bit check.

Enumerate 16 valid codewords by known-bit distance; correction/erasure reconstruction only for a unique candidate with `2e+s<dmin`. CR4/5 and CR4/6 do not claim one fully observed bit correction. Detection mode preserves systematic candidates/failures; assumed radius is not proof of actual error count. Keep received/masks/corrected/nibbles/bytes/dependencies and separate head/payload distances/correction counts.

No PHY CRC stays candidate; parity-failed but CRC-consistent systematic bytes remain weak. Bad/unknown explicit header cannot supply trusted length/rate. Implicit-header check is not-applicable; uncomputed CRC is not a fake zero. Distinguish truncation from recoverable erasure and actual received symbols from padded slots. Empty payload retains stage/head evidence without inventing a byte frame.

Codec capacities/fixed state fit 64 KiB workspace/8 MiB results under own limits; I/Q FFT/search uses its separately accounted 1 MiB. Global preamble/frame constraints used for hypothesis selection must remain source dependencies, not only nearby final symbols.

Reference: author-maintained [LoRaPHY](https://github.com/jkadbear/LoRaPHY) / [public implementation/vector](https://github.com/jkadbear/LoRaPHY/blob/master/LoRaPHY.m). Tests use independent sender/codebook/interleave/GF(2), not that decoder as oracle. Public SF12/LDRO vector recovers 01..09 and CRC BA2E. Cover declared combinations, short/max payload, errors/erasures/strict-candidate-truncated/large support/budgets. Codec and private-RF I/Q acceptance are separate evidence.
