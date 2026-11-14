# Receiver capabilities and configuration dependencies

English · [简体中文](13-接收机能力与配置联动.md)

Receiver providers own RTL-SDR/SDRplay capability discovery, parameter dependencies, application/readback and recommendations. Additional RF/LNA, Bias-T, notch and HDR controls require their own declared support; the existence of generic settings is not proof that each is implemented.

## Boundaries

Desktop queries devices through an isolated `signalscope` process. Sessions use the receiver registry. Acquisition, main/auxiliary analysis and recording have independent bounded queues. Vendor libraries link only to receiver providers; generic UI does not implement device rules.

`adapters/receivers/configuration` describes serializable hardware facts/plans without Qt or detectors. `backend/sessions/receiver_configuration` adds selected-analysis requirements and is shared by preview, CLI validation and startup. `configure` and streaming share application/readback. Since SDRplay initializes hardware at activation, configure briefly activates, verifies startup, reads again and closes without delivering/recording I/Q. Startup later configures again; close need not preserve settings.

## Capabilities and attribution

`probe` retains old fields and adds capabilities v1: model, sample format, continuous/discrete rates, common/recommended rates, tuning ranges, IF options, antennas, manual gains and origin. Recommended values do not filter the legal capability set; continuous ranges accept unlisted integer rates.

SDRplay primarily queries SoapySDR. RSPdx/R2 antenna C limits and nominal ADC bits supplement driver facts from manufacturer specifications. ADC bits are not ENOB, stored I/Q encoding or an observed internal ADC clock. Below 2 MS/s, do not infer ADC bits.

librtlsdr lacks generic tuning-range enumeration; the provider derives nominal ranges from the reported tuner, leaving unknown models unknown until application checks. Only matching HackerGadgets AIO V2 USB identity enables its documented 100 kHz–28.8 MHz direct path. Automatic HF outside tuner coverage uses Q, with explicit I/Q choices available; readback verifies mode and tuner gain is inapplicable in direct sampling. This is not passband/sensitivity calibration.

RTL legal rate intervals are 225001–300000 and 900001–3200000 S/s. AIO2 recommends 2.56 MS/s while retaining 2.048/2.4/2.56/2.8/3.2 MS/s choices; above 2.56 MS/s a continuity risk is shown. No hardware sample sequence means ordinary capture cannot report exact USB sample loss; `rtl_test` evidence is separate.

Sources: [librtlsdr API](https://github.com/osmocom/rtl-sdr/blob/master/include/rtl-sdr.h), [tuner overview](https://osmocom.org/projects/rtl-sdr/wiki/Rtl-sdr), [RSPdx-R2 specification](https://sdrplay.com/wp-content/uploads/2026/06/RSPdxR2_Datasheet.pdf), [Soapy settings implementation](https://github.com/pothosware/SoapySDRPlay3/blob/master/Settings.cpp). Specification coverage is not measured RF coverage.

## Whole-configuration planning

Plans expose `hardware_valid`, overall `valid` and scoped issues. Hardware rules cover rate, input, tuning, antenna frequency, IF and manual gain. Inactive tuner gain under auto/direct mode does not block the plan. Search width can reach the sampled complex spectrum; out-of-spectrum requests fail. Internal rate conversion satisfies LoRa integer symbol timing; signal-edge margin remains separately disclosed.

Automatic IF follows the driver's compatible policy: largest reported width not exceeding rate (2/3 MS/s → 1.536 MHz; 10 MS/s → 8 MHz), or smallest width below its minimum. Explicit wider reported IF remains available with aliasing risk. Nominal 3 dB width is not a hard truncation of the sampled spectrum; edge attenuation is explained. Readback is actual where available; RTL filter readback can remain unknown. CS16 is four bytes per pair, independently of ADC bits.

No global 3.2 MS/s restriction applies to monitoring, scan, replay or FM. RSP can acquire/display/analyze a requested 10 MS/s spectrum. Overlapping channelization and anti-alias decimation serve suitable detectors; original-spectrum OFDM/CSS remain separate. Load, queues, skips and losses are visible; do not silently shrink coverage or turn off selected detectors.

## Desktop behavior

Receiver settings uses two resizable/scrollable regions (parameters/repairs and modulation assessment), stacked below 1000 px. Device/overall status stays at top; technical details collapse; unrelated valid controls do not all turn error-colored.

Shared search-width controls offer presets, current maximum and custom integer kHz. Maximum follows rate rounded down to kHz. Fixed/custom excessive requests remain visible as conflicts rather than clipped. Mode and value persist. Advanced integer S/s entry retains real hardware range.

Recommendations preserve frequency/detectors and use provider candidates: RSP typically 10 MS/s/10 MHz, AIO2 2.56 MS/s/2.56 MHz, auto gain/IF/input and compatible antenna only when needed. Run the same complete preflight as startup; no feasible plan means no recommendation. Capability recommendation is not a real-time guarantee.

Apply is enabled only for a valid full plan and checks again when invoked. Asynchronous application disables editing/cancel until readback completes. Only success saves selection. Summaries/tooltips and CLI/session diagnostics retain request vs actual. RTL auto-gain acceptance is labelled accepted by driver when no readback API exists.

## Detector assessment and recovery distinction

Assessment comes from the registry: LoRa, FSK/GFSK, OOK, ASK, FM, BPSK/QPSK, 4-FSK, OFDM, CSS and later registered descriptors such as DTMB. It follows actual selections and reevaluates per row, while the full session must satisfy all enabled detectors. A disabled or out-of-band detector does not itself invalidate hardware.

FM observation checks 65–110 MHz with a complete 200 kHz channel. PSK/4-FSK candidate snapshots use an approximately 38 kHz channel filter. CSS needs repeated chirps spanning at least 40 kHz; OFDM examines full-spectrum snapshots. These are detector conditions, not universal recognition claims. Detector structure-only output remains distinct from the later independent LoRa/PSK/OFDM recovery providers; see specifications 24/25.

```sh
signalscope probe --receiver sdrplay
signalscope validate --receiver sdrplay --purpose monitor --rate 2048000 --bw all
signalscope validate --receiver sdrplay --purpose capture --rate 10000000
signalscope configure --receiver sdrplay --rate 6000000 --gain auto --antenna 'Antenna B'
signalscope capture --receiver sdrplay --rate 10000000 --seconds 1 --output capture-wide
signalscope validate --receiver rtlsdr --rate 2400000 --gain auto --lora 0
```

Invalid validation returns structured JSON and nonzero status before creating a monitoring directory. SDRplay `activation_confirmed` distinguishes hardware startup from cached initial values.

Regression covers tuner differences, rate gaps, nonrecommended settings, antenna boundaries, gain modes, timing/coverage, encoding/ADC distinction, serialized facts, fake-driver application/readback and GUI application. These do not replace RF/USB/sustained hardware tests. Vendor-module conditions can skip fake-driver desktop checks. Explicit authorized `AMC_TEST_REAL_SDRPLAY=1 receiver_dialog_test` requires RSPdx-R2 and really applies 3 MS/s / antenna B; `AMC_RECEIVER_DIALOG_SCREENSHOT` exports the window.

## Independent receiver services (0.2.12)

The base application and optional SDRplay service-client toolbox are separate
delivery units. Vendor API/driver loading stays in the independently installed
receiver service. Provider-owned connection descriptors and request protocol 2
carry the address before enumeration; UI/sessions/CLI remain generic. The pinned
BSL SoapyRemote client needs SoapySDR 0.8.1 and supports the offline source override
`FETCHCONTENT_SOURCE_DIR_SOAPYREMOTE`. Only declared bundled providers enter the
base archive; raw CMake installation is a staging tree, not a qualified base
archive. See [receiver services](receiver-services.md) for SDRconnect assessment,
setup, protocol, continuity and license boundaries.
