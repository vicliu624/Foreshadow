# Algorithms and testing

English · [简体中文](04-算法与测试方案.md)

Acceptance combines independent mathematical truth, false-positive cases, provider/resource extension contracts, operational tests, native desktop procedures and real installed packages. Tests must not generate expected results by calling the receiver algorithm under test.

## Signal truth and interpretation

LoRa testing searches unknown time/frequency/BW/SF and both I/Q polarities. Repeated chirps alone support CSS candidacy; preamble/sync/SFD support LoRa structure. PHY/application checks supply separate later evidence. Retain weak/edge failures and periodic-FSK counterexamples.

PSK truth covers rectangular and independently generated RRC pulses, timing/carrier offsets, phase, clock error, multipath and noise. OFDM truth uses explicit inverse DFT or continuous-time carrier evaluation rather than receiver synthesis. Check each eligible payload decision, independent channel response and pointwise recomputed EVM. Known payload references must not change synchronization/channel estimates.

Negative cases include silence/noise, CW, wrong training/models, deep nulls, missing samples, truncated frames, invalid parameters, cancellation and budgets. A method must preserve unknown values and failed checks rather than fabricate data.

## Test families

| Area | Representative contracts / checks |
|---|---|
| Core/encoding/files | `core_contract`, `sample_codec_contract`, CLI roundtrip, source/clock/window and recording checks |
| LoRa | Candidate/paired-IQ/recovery-IQ/codec contracts, real weak/edge fixtures and periodic-FSK negatives |
| Waveform processing | Signal processing, recorded channel, waveform/window analysis, signal capture, recorded worker and save worker |
| Models | Reference model/builtin/digital/parameter contracts and native model workflow |
| Recovery | `recovery_contract`, `coherent_recovery_contract`, native recovery workflow and independent measurement/byte/vector providers |
| Architecture | Target/header dependency rules, extension-contract checks, implementation-ID routing limits and resource keys/placeholders |
| UI | Generic parameter dependencies, workspace/panel extensions, unknown events, language fallback, save/reopen and evidence location |
| Broadcast | FM continuity/output isolation, DTMB detection/native receiver/FEC, real codec references and media timing |
| Distribution | Catalog signatures, compatibility, paths, resource declarations, transactions, concurrency, activation and rollback |
| Packaging | `check_packaged_receivers.cmake`, `check_desktop_package.cmake`, Mac bundle/Mach-O audit and actual package installation |

Use `ctest --test-dir build -N` for exact names in the configured build. Optional modules and platform conditions change the test count.

## Native and package acceptance

Run native wide, requested 1.25-scale, and 640×480 workflows. Check actual visible data, parameters, errors, clicking known complex points, precise source location and persisted layouts across restart. A PNG merely existing is insufficient. Separate component harness results from full-product startup and deployment.

Build/install from a frozen source identity. Archive and extract to a Chinese/non-BMP path, discover actual modules, install independent toolboxes, reopen examples, export evidence and inspect package runtime closure. Keep signing keys outside source and build jobs.

## Real-time evidence

Exercise sustained input, short reads, independent bounded streams, overload, all selected analysis stages and UI stalls. Report offered/accepted/rejected counts per stage and actual memory budgets. Synthetic duration is not RF/hardware continuity; short zero-drop runs do not establish 24-hour losslessness.

The opt-in `signal_capture_test --sustained-seconds 180` keeps normal CTest short. Physical hardware tools require explicit invocation and are outside ordinary CTest. Controlled Pager TX receipts prove chip completion, not receive identity or payload success.

## Running and reporting

```sh
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
python tools/docs/check_docs.py
```

Use `-C Release` with multi-configuration builds. Retain initial failures and justified corrections; reruns do not erase earlier evidence. Report passed, failed, skipped and externally blocked separately with source/environment/input identity. See the [report index](README.md) and [current requirements](01-product-requirements-and-acceptance.md).
