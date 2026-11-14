# Wideband reception and multirate analysis

English · [简体中文](14-宽带接收与多速率分析.md)

Model acquisition rate, sampled spectrum, nominal IF 3 dB bandwidth and each detector's processing rate separately. RSPdx-R2 and uConsole AIO V2 capabilities replace the inherited universal 3.2 MS/s monitoring cap and fixed 1 MHz recommendation.

## Required behavior

1. Allow supported RSP wideband rates including 10 MS/s. Evaluate AIO2 with its actual RTL tuner/driver/input mode; do not exchange device limits.
2. Search can span the requested sampled spectrum. IF rolloff and detector edge margin explain effective coverage, not an artificial global ~1 MHz cap. Reject genuinely unsupported requests.
3. Raw recording and spectrum keep full original I/Q. Channelized/resampled analysis must cover outer bands as well as the center.
4. Channelization is anti-aliased and phase/history continuous, supports noninteger ratios, gaps and tails, and maps events precisely to original frequency/time/recording positions.
5. Internally resample LoRa-compatible 2.048/2.4/2.56/3.2 MS/s rather than rejecting legal hardware rates for integer-symbol requirements.
6. Publish coverage, queues, skips/rejections and load. Never hide throughput shortage by shrinking bands, fabricating losslessness or disabling selected detectors.
7. Recommendations, UI, assessment, CLI and hardware readback use consistent provider-owned rules; the full legal hardware range remains selectable.
8. Validate both devices' settings, capture, coverage and performance separately, then package/deploy under the existing release process.

## Processing

Acquisition retains original rate/center/encoding. Raw recording and bounded-rate spectrum are independent. A shared forward FFT translates/low-passes/decimates overlapping channels, with fractional conversion as needed. Deduplicate overlaps; disclose edges that cannot fully contain signals.

LoRa uses a bounded shared worker pool, not nested pools per channel. Its analysis clock is four times the largest selected BW, with direct narrow/low-rate compatibility paths retained. High-rate FSK/OOK/ASK/FM use 2 MS/s channels; low-rate input retains its clock and aligns to 256-point FFT steps so short reads/arbitrary chunks/fractional timing do not break amplitude history. FM audio first decimates to 400 kS/s. High-rate PSK/4-FSK candidate channels decimate to 80 kS/s; CSS/OFDM keep the original spectrum. Snapshot candidate processing is separate from trained waveform recovery.

Shared FFT channelization performs low-pass frequency-domain decimation and optional fractional conversion. FFTW falls back to KissFFT; `SIGNALSCOPE_FFTW=OFF` verifies that path. Planning/destruction is serialized across DSP/modulation while execution can remain parallel. LoRa receives float samples without repeated intermediate CI16 quantization; original recording format is unchanged.

Channel spacing follows maximum BW but outer coverage includes positions that fully contain the smallest selected BW. Frequency shifting clips cyclic FFT indexes outside original RF support rather than wrapping opposite-edge energy into a channel. If a coarse LoRa candidate lands on the second down-SFD symbol, the previous symbol is rechecked using the same preamble/sync/two-down/quarter-tail/transition conditions; thresholds are not lowered.

Recommended width selects “current maximum,” following later rate changes. Fixed/custom values remain requested and explicitly conflict when excessive. IF 3 dB width, nominal ADC bits, I/Q format and channel count are visible; runtime publishes CPU, discarded analysis, auxiliary skips and snapshot busy counts.

## Evidence

Use independent signals at band ends and channel boundaries, out-of-band rejection, arbitrary chunks, gap reset, fractional rates and exact time/frequency mappings. Verify original recording bytes/indices independently. Hardware reports retain USB/driver identities, applied settings, throughput, CPU, queues and actual coverage. Static capability, short runs and long-term evidence are distinct.

References: [AIO V2 product](https://hackergadgets.com/products/uconsole-aio-v2), [manufacturer setup](https://hackergadgets.com/pages/hackergadgets-uconsole-rtl-sdr-lora-gps-rtc-usb-hub-all-in-one-extension-board-setup-guide), [RSPdx-R2 specification](https://sdrplay.com/wp-content/uploads/2026/06/RSPdxR2_Datasheet.pdf). Manufacturer power/HF descriptions do not replace actual enumeration, passband or sensitivity measurements.
