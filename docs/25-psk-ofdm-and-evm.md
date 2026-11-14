# PSK / OFDM reception and EVM

English · [简体中文](25-PSK与OFDM接收及EVM规格.md)

Independent receiver providers extend explicit reference comparison. Windows/Linux real review packages and subsequent Intel Mac native acceptance are recorded in the [receiver report](reports/2026-11-12-psk-ofdm-acceptance.md) and [Mac report](reports/2026-11-12-native-macos-deployment.md). Earlier Mac deferral is historical.

## Ownership and extension

Change axes are receiver method, training/pilot protocol, complex measurements and presentation. Providers under `plugins/research/recovery` own parameters/full validation, synchronization, channel estimation, equalization, decisions and measurements; registry is the assembly point. Generic DSP recovery owns result contracts. UI, save workers and export are stable consumers without PSK/OFDM/provider-ID branches. Provider text ships in its catalogs.

Recovery v2 adds domain-labelled complex sequences with per-point group/signed coordinate/validity/dependencies and explicit generated-reference identity. Reference cannot become an observation leaf. v1 providers/archives retain compatibility. Evidence v2 and analysis v8 explicitly declare the capability; old consumers reject unsupported versions rather than silently drop complex data.

## Receiver conditions

Automatic synchronization assumes a declared waveform family/training configuration, not arbitrary blind air-interface decoding. BPSK/QPSK declares symbol rate, pulse shape, training and payload length. Acquisition searches frame start, fractional timing and carrier; training estimates effective channel/finite linear equalizer, followed by payload phase/timing tracking.

OFDM declares FFT/CP, two repeated training symbols, ordered active carriers and pilots. CP/training synchronization, CFO correction, per-carrier channel/equalization and pilot common-phase/clock-slope tracking are automatic. Out-of-domain, lost-lock and deep-fade points remain unknown.

Training/pilots estimate the receiver. Known payload is measurement-only and must never enter synchronization/channel/equalizer fitting. Without it, decision-reference EVM can be available with provenance and BER limitation. Measure equalized/phase-corrected **data**, excluding training/pilots/invalid/transient points.

RMS EVM = `100 sqrt(sum |z-r|² / sum |r|²)`; peak uses the same average reference power. Fewer than eight valid points, more than 20% excluded data, or lost lock means no usable EVM. Show valid/excluded counts. Low EVM does not validate CRC/application data; bytes remain candidates until their own checks.

Effective channel is under the analysis clock/declared pulse, not calibrated RF transfer. Point support includes actual observation and estimation training/pilot support; no gap-filling or fake single-sample timestamp.

## Acceptance

Independent transmitter truth covers fractional/integer timing/CFO, phase, sample error, multipath/noise, held-out payload decisions and EVM. Negatives include wrong training, no signal, fades, discontinuities, cancellation and limits. Receiver code must not synthesize its own expected answers; fitting residual is not data EVM.

An independent non-modulation provider exercises complex protocol, generic UI, export, persistence and per-key language fallback without host ID edits. Budget result/workspace capacities, search/matrices/FFTs and JSON bytes/nodes/depth separately. Plot at most 16384 measured/reference points; full tables/evidence and EVM use full eligible data. Run architecture/contracts/regression plus real Unicode/non-BMP installed packages, native save/reopen/location/export.

References: [MathWorks EVM](https://www.mathworks.com/help/comm/ref/evmmeasurement.html), [GNU Radio Symbol Sync](https://wiki.gnuradio.org/index.php/Symbol_Sync). These definitions do not establish air-interface standard compatibility.
