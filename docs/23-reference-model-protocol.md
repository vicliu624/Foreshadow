# Reference model and comparison protocol

English · [简体中文](23-参考模型与对照协议规格.md)

This implements laboratory P2, not identification/decoding. Automatic PSK/OFDM receivers, channel estimation/equalization and symbol-domain EVM belong to separate recovery providers in specification 25. Explicit time-domain comparison retains its definition; residuals are not renamed EVM.

## Ownership and versions

Providers own model descriptions, whole-parameter validation, generation, physical-parameter estimation, method explanation and resources. Generic comparison consumes prepared pure functions/protocol fields. UI/workers/parameter controls/views import no concrete model and add no ID branches. Assembly is `plugins/research/models/registry.cpp`; independent providers use the existing comparison path.

Generic fitting owns explicit manual time offset, bounded integer-delay search, optional complex gain and optional complex DC. Draft vs applied state stays separate; dependencies use the entire pending configuration and actual input clock.

- **v1:** versioned descriptions/preparation produce complete parameters/method and reusable pure sample function or resource-keyed issues. Evaluation receives relative integer position, actual rate and explicit offset. Time origin is selection start; never convert absolute source index to double.
- **v2:** whole-draft descriptions, legal alignment domain and provider workspace declarations. Old v1 APIs remain; missing v1 workspace means unknown, not zero or a full desktop budget guarantee. Preparation checks remaining budget; closure/prefix/state and generic output/candidate capacities count. Invalid UI values remain visible instead of clamped.
- **v3:** keeps v2 but separates integer delay from manual seconds. Symbol boundaries must not round-trip integer samples through floating seconds. New implementations use `offset_samples()`/`seconds()`. Legacy v1/v2 receives the original combined seconds and zero new integer field. Record protocol/delay encoding; version dispatch is generic, not model-ID dispatch.

## Result and fitting semantics

Retain immutable observation, exact interval, provider/method/parameter versions, alignment/fitting, compared count, reference/residual traces, fitted complex coefficients and errors. Reference is generated data, not a second raw observation. Allocate only selected actual points using AnalysisTrace v4 compact mapping; original/rational coordinates remain unchanged. Gaps are not zero-filled; time across a gap still follows the parent clock. Low amplitude can invalidate phase/frequency inference without prohibiting raw complex comparison.

Fit `sum(|y-g*x|²)`, or `sum(|y-g*x-d|²)` with DC. Reject zero reference energy, indistinguishable reference/constant, nonfinite outputs or budget failure. Every delay uses identical observation support/freedoms; rank actual residuals and preserve ties. Pure-tone delay with free phase is usually ambiguous; do not claim unique synchronization from an arbitrary tie.

Report complex residual RMS and ratio to input RMS, with zero-input ratio unavailable. Neither is EVM. PSK/OFDM EVM requires its own known reference, synchronization/equalization/valid-region evidence. Generating/fitting noise proves neither carrier nor SNR nor business meaning.

Count actual vector capacity and combined live result payload. Limit candidate count, evaluations and time domain; cancellation publishes no partial result. Reuse the existing serial worker rather than retaining multiple generations in separate pipelines.

Raw/channel analyses are immutable reusable inputs. Window and model have separate applied intervals; updating one must not discard/recompute the other unnecessarily. Execute base processing/window/model in order. Before model work, reserve actual still-live waveform/channel/window payload and then generator/output, not permanently reserving finished FIR/FFT temporaries. v1 numerical compatibility does not pretend to offer v2 joint-budget guarantees.

## Desktop and analysis v6

Model comparison is a real laboratory result page with registry-driven provider controls. Choose raw or channel input and apply to that input's current view through the serial worker. Show observed/reference I/Q, independent residual traces and complex plane, with generated/observed labels.

Applied time origin is fixed. Zoom only views/locates support; it does not refit. Uncompared residuals are unavailable. Distinguish fixed delay, ambiguity and uniqueness only within the finite search domain; none alone is symbol lock. Save completed-result parameters/freedoms, never drafts. Cancel/error/open failure preserves the completed result. Replacing base input invalidates old models; loading original neighborhoods can preserve/recompute a processed model with unchanged chain/clock.

`org.amc.waveform-analysis` v6 retains v1–v5 sources, relative references, identities, processing and views. Model config v1 stores provider/method, whole parameters, input kind, decimal-string applied interval, actual clock, all fit options/limits, opaque method record, and separate view/cursors/page. Recorded error/fit facts are historical results; reopen still validates input/recomputes. Unknown provider/method, mismatched clock or invalid full parameters cannot silently select a default. The saver owns generic source/atomic transactions, not model parsing.

Model/window pages omit raw/channel-only overview/zoom controls. Compact traces/parameters scroll independently and explanations stay in parameters so the plot remains usable. Generic workspace still owns docking/visibility.

## Built-in model conditions

Initial providers: constant complex carrier, sinusoidal AM, sinusoidal FM, continuous-phase rectangular-frequency 2-FSK, nonrepeating linear chirp, and repeating single-symbol LoRa chirp. FSK bits are explicit; LoRa SF/BW/symbol/direction is explicit with no packet structure/decoding. LoRa integrates continuous-time phase through wrap ([Chiani/Elzanaty](https://arxiv.org/html/1906.04256#S2.SS1)). Sweep width is not measured occupied bandwidth. Text/fields/issues/directions ship with each provider.

Two v3 providers add known-symbol PSK and known-subcarrier OFDM through the same consumers:

- PSK M=2/4/8/16 maps explicit decimal indices to `exp(j*2*pi*k/M)`, not inferred bits/Gray mapping. Rectangular holds symbols; continuous RRC is centered at `k+0.5`, rolloff 0–1 and symmetric span 2–32. Untruncated energy is one in symbol-time units; truncation is not renormalized. Finite sequence must cover all alignment and RRC neighbors; repetition is explicit only.
- OFDM supplies `bin:real,imag` pairs separated by spaces, symbols by `|`; unspecified bins are zero reference. N=8–4096, CP=0–N, actual input rate and spacing Fs/N. Use unitary `sum(X[k]*exp(j*2*pi*k*n/N))/sqrt(N)` and copy the symbol tail for CP. Outside finite symbols there is no reference, so insufficient coverage fails. [OFDM background](https://www.mathworks.com/help/comm/ref/ofdmmod.html); the normalization is explicitly this formula.

PSK max 256 known symbols. OFDM max 16 symbols, 64 specified bins each, 256 total. Each text max 4096 bytes; OFDM components −10…10, duplicate/nonfinite values rejected. Bound counts before allocation, then account capacities. RRC ≤33 terms/evaluation; OFDM ≤64; evaluation limits count calls, not fixed cost or sustained performance.

These models record supplied reference/manual boundaries and the synchronization/equalization they do **not** establish. Time-domain OFDM includes CP; no model-selected symbol/carrier EVM is displayed. [EVM definition](https://www.mathworks.com/help/comm/ref/comm.evm-system-object.html) does not convert residual/input RMS into EVM. Later symbol-domain measurement needs explicit protocol/domain/validity and actual receiver evidence, not a checkbox/model name.

Acceptance covers independent formulas, gain/DC, wrong reference, gaps, huge source indices, rational clocks, tied delay, degenerate reference, zero input, limits/cancel, independent models/resources and real UI/save/reopen. Field recordings exercise workflow/negative evidence, not unknown device attribution.
