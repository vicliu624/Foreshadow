# Delivery and user guide

English · [简体中文](09-交付与使用说明.md)

Use the [README](../README.md) for platform installation/build instructions and a full screenshot overview. This guide describes the actual operating sequence. Historic download artifact IDs and private build paths are no longer the current installation entry; use the repository Releases and dated evidence index.

## Choose the right input

- **Live receiver:** requires the installed provider and actual device or receiver service. Local hardware drivers belong on the machine that owns the device. Configure and apply in Receiver settings before starting.
- **I/Q recording:** open its SigMF metadata file; retain the associated sample data. A recording's own clock/encoding is authoritative.
- **Saved analysis:** use **Open analysis** in Waveform laboratory; this restores applied processing and recomputes from validated source data.
- **Transport stream:** use the television panel's file source; this tests transport/media rather than RF synchronization.

A bare binary file without explicit format/rate/source metadata must not be guessed. The source provider determines supported containers. Offline work does not require a connected receiver.

## Connect an independent SDRplay service

From 0.2.12 the base and SDRplay client are separate packages. Install **SDRplay Support**, then open **Receiver settings → SDRplay**, enter **Receiver service**, and choose **Refresh devices**. Select the device, antenna, rate, gain and advanced settings; apply the entire configuration and inspect actual readback before starting. The endpoint is saved with the receiver settings. Configure it once on a new installation; a configured workstation retains its saved address across restarts.

The receiver computer runs independently installed SoapySDRServer with the user's vendor API/driver; Foreshadow does not load those local hardware components. Stop the current hardware user before starting a second client. Service setup, trusted-network requirements and the SDRconnect comparison are in [Receiver services](receiver-services.md); actual package/hardware evidence is in the [r4 report](reports/2026-11-14-receiver-service-review.md).

## Prepare the workspace

Choose **View → Layout → Waveform research layout**. The main laboratory and **Measurements and research** are separate panels. View restores closed panels. Dock/floating/tab state persists; use the named preset explicitly when you want to rearrange it. Resizing does not reapply presets.

On a narrow screen, use tab groups and scroll parameter/complex-result panes. Closing or hiding a panel does not necessarily stop its running task. Stop capture/audio/monitoring using its own action; disconnect releases all receiver users.

## Raw observation and source coordinates

Open the source, enter file-first-sample/count where available, then load the range. The overview gives context; the detail region can zoom to individual samples. Click/drag places B; Shift-click places A; right-drag selects a time range; wheel or +/− zooms; Home restores the loaded window.

Inspect I/Q, amplitude, phase (wrapped/unwrapped), and instantaneous frequency. The inspector reports original/file sample coordinates, values, validity and A/B differences. Integer source positions are preserved even above exact floating-point integer range. Phase unwrap and smoothing restart at gaps; low amplitude makes phase/frequency invalid. “Computable” does not establish a carrier or adequate SNR.

## Live capture and freeze

In Capture, request an acquisition compatible with the provider's demand. Inspect requested/actual settings and startup calibration/ADC facts. Start bounded capture, observe accepted/rejected input and retention eviction, then freeze a useful interval. Analyze the immutable frozen observation while other live users can continue. Resume or recapture explicitly; a source/session change must not reuse stale results.

Startup facts are scoped before first samples. They do not prove the frozen interval was continuously calibrated or free from ADC overload. Unknown facts remain unknown after save/reopen.

## Target channel and window research

Select time/frequency and configure translation, low-pass and resampling in Selected channel; apply. Inspect bandwidth, filter delay/support, rational source mapping and invalid edges. Compare raw and processed views. A selected-channel sample can depend on many original samples.

Window research supplies STFT, PSD, autocorrelation, frequency histogram and statistics. Select raw or processed input, configure window/function/hop and apply to the intended interval. Power is digital with declared units. No gap is zero-filled to manufacture a valid spectrum. Render aggregation does not change measurements.

## Reference model workflow

In Model comparison, enable comparison, select observed input and registered model, edit provider parameters and permitted alignment/fitting, then apply. Current input view becomes the applied interval. View observation/reference overlays, residuals and complex planes. Generated reference is explicitly labelled.

Draft edits are separate from completed results. Zoom changes viewing, not fit parameters/time origin. Error/cancel retains the last completed result. A new source or changed processing invalidates incompatible results. Save persists applied parameters, not an unapplied draft. A residual is not EVM or identification.

## Recovery workflow

In Evidence and recovery, enable recovery, choose original/processed input and the provider, supply its full waveform/framing parameters and apply. The result pages expose Hex/ASCII, frames/checks, symbols/bits/values, stages/event, and complex measurements when supported.

- Check candidate/validated/repaired/truncated states before interpreting bytes.
- `??` is unknown data and cannot become invented binary output.
- CRC absent, not applicable, failed, unknown and passed are distinct.
- Click/double-click a supported value or use Locate input to return to the dependent interval; processed/original support remains explicit.
- Export evidence JSON for complete structure. Byte export is limited to eligible known values and retains its candidate interpretation.

Bounded FSK requires an explicit symbol grid/frequencies/framing rule. LoRa PHY recovery needs its declared radio parameters. The paired family additionally requires its explicit empirical encoding/check assumptions; it does not name a universal application protocol or infer device ownership.

## PSK/OFDM review sequence

1. Generate or open case 05 (QPSK) or 06 (OFDM) from the native synthetic review exporter.
2. Open the analysis; verify source, provider and applied parameters.
3. Inspect lock/frequency, valid/excluded data counts and EVM reference origin.
4. In Constellation/channel, select the measurement plane and group. Dots are measured data; crosses are generated reference.
5. Inspect the channel estimate and source dependencies; locate a measured point in the waveform. Use scrolling on compact layouts.
6. Save to a new analysis, reopen, export evidence JSON and eligible bytes. Check that source identity and declared method are preserved.

The reference data is measurement-only, not training for synchronization/equalization. Wrong training, deep fades, gaps, too few valid points or excessive exclusions must not produce a trustworthy EVM claim. See [exact conditions](25-psk-ofdm-and-evm.md).

## Monitoring, recordings and events

Select detectors, configure the receiver and start monitoring. Review stage-specific queues/losses and generic event evidence. Manual recording and LoRa-triggered recording retain actual I/Q. Export important recordings outside the quota-managed store.

Open a recording for software replay, pause/resume or export slices. Replay time is not original RF time. An unknown event ID still displays its description/features; it must not be hidden or forced into a known detector category.

## FM, television and packages

Show FM or DTMB through View. Broadcast can request its own receiver lease without monitoring. Supply the actual tuning and output settings, then start. Conflicting requests preserve existing users and report the conflict. TV accepts live/recorded I/Q or TS, and lists programs/audio only after valid table recovery. Stop a task rather than merely hiding its panel.

Plugins → Toolbox Center lists trusted official packages, dependencies, versions and restart requirements. Install/update/enable/disable/uninstall changes are blocked while conflicting work is active. Offline import is still catalog-authenticated. Application activation confirms startup and rolls back failure; offline research continues if network checks fail.

## Troubleshooting

| Symptom | Next check |
|---|---|
| No device | `receiver-plugins`, provider diagnostics, device/serial; for a service check its saved address/server, and driver/API/USB access on the receiver computer |
| Service connects but no samples | Hardware ownership, server diagnostics, negotiated stream/status ports and network reachability; forwarding only control port 55132 is insufficient |
| Cannot apply configuration | All pending fields/dependencies, antenna/tuning/rate rules and selected analysis compatibility |
| No detected events | Actual acquired band/IF, enabled detectors, continuity/load and evidence thresholds; absence is not proof no RF exists |
| Recording appears incomplete | Source gaps, pre-trigger retention, late analysis, cap/quiet-tail flags and writer errors |
| Model/recovery fails | Correct source/range/clock, applied complete parameters, training/reference support, continuity and budgets |
| EVM unavailable | Lock, reference origin, at least eight eligible data points and exclusion fraction ≤20% |
| Saved analysis cannot reopen | Both source files present, identity unchanged, supported provider/method/version, valid relative paths |
| Hidden panel | View entry or explicit restore-default preset; do not delete preferences as a first step |
| Update fails | Network/proxy, trusted catalog/platform compatibility, writable root, active task conflicts and helper diagnostics |

See [validation reports](README.md), [storage rules](08-continuous-monitoring-and-event-storage.md), and [architecture constraints](16-extension-boundaries.md). Report source/build identity, exact steps and diagnostics; do not include credentials or private RF recordings in public reports.
