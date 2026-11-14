# Two-receiver wideband acceptance

English · [简体中文](2026-11-09-双接收机宽带验收.md)

Historical report,9 November 2026: provider-declared configuration, validation and deployment for RSPdx-R2 and HackerGadgets uConsole AIOV2. The four-core uConsole did not define a cross-platform device limit. Backlog/gaps/skips/source errors expose insufficient throughput; the application must not silently shrink coverage or disable selected detectors. Lossless execution of every detector combination on this host was not an acceptance requirement.

## Configuration and DSP

| Receiver | Recommended rate/search | Device facts |
|---|---|---|
| RSPdx-R2 |10 MS/s /10 MHz |Automatic/manual IF; automatic8 MHz 3 dBIFat10 MS/s, edge rolloff and nominal ADC bits disclosed |
| AIOV2 |2.56 MS/s /2.56 MHz |Hardware2.048/2.4/2.56/2.8/3.2 MS/s retained; 3.2 MS/s USB continuity warning |

Recommendation preserves frequency/detector selection and chooses current-maximum width, which follows rate changes. Fixed/custom requests remain visible and block application when invalid. IF3 dB width is not a hard digital-search cutoff. Actual USB manufacturer/product identifies AIO2;100 kHz–28.8 MHzI/Qdirect sampling disables irrelevant tuner gain. UI displays data volume, codec, nominal ADC resolution, IF and internal channel count, with compatibility/parsing scope for ten modulation classes rather than invented success probability.

Shared forward-FFT overlapping channelization, anti-alias decimation and fractional conversion preserve requested coverage/source coordinates. Outer-channel cyclic indexing was corrected to avoid wrapping the opposite RF edge into a channel; multiple-BW edge coverage includes the smallest fully fitting model. SFD coarse candidates landing on the second downchirp now recheck the previous symbol under the same structural thresholds.

LoRa channels share workers and receive floating samples directly. FFTW planning/destruction is locked, execution remains parallel; KissFFT is a fallback. Narrow input keeps256-point FFT steps across short reads/arbitrary block boundaries. High-rate snapshots/FM use corresponding filters. Linux analysis lowers scheduling priority, without downsampling raw acquisition/spectrum/independent recording. UI distinguishes delayed results, analysis gaps/possible misses and actual source overflow/incomplete-stop, including lag/loss/busy/CPU.

## Automated and physical checks

Local Release32/32 passed. Three KissFFT wideband cases passed ASan/UBSan. Native uConsole 29 passed/3 SDRplay-environment skips/0failures, plus separate English/Chinese real RSP/AIO2 settings checks. New truth covered10 MS/s±4.9 MHz,2.56 MS/s±1.19 MHz, both polarities, multiple BW/longSF, original clocks, deduplication, fractional rates, anti-aliasing and RF-edge isolation, plus all supported modulation examples. Existing false-positive/recording/localization/GUI checks remained.

Debian ARM64/Qt 6.8.2/API 3.15/librtlsdr2.0.2completed28physical checks: capability/preflight/apply/readback and AIO2 automatic/I/QHFsampling. One-second raw captures:

| Device/rate | Complex samples | Bytes |
|---|---:|---:|
| RSP 2 MS/s |2,000,000|8,000,000|
| RSP 6 MS/s |6,000,000|24,000,000|
| RSP 10 MS/s |10,000,000|40,000,000|
| AIO2.048 MS/s |2,048,000|4,096,000|
| AIO2.4 MS/s |2,400,000|4,800,000|
| AIO2.56 MS/s |2,560,000|5,120,000|
| AIO 3.2 MS/s |3,200,000|6,400,000|

## Five-second host-load observations

Wall/CPU include startup/drain; CPU excludes independent SDK services. Recording-path processing is not a claim that normal event-mode monitoring saved every raw byte.

| Case | Wall s | CPU s | Input pairs | Main rejected | Narrow skipped | State |
|---|---:|---:|---:|---:|---:|---|
| RSP 10M, LoRa |10.91|21.58|50,000,000|42,381,440|0|stopped|
| RSP 10M, narrow |7.87|7.45|50,000,000|0|27,504,768|stopped|
| RSP 10M, combined |11.26|23.69|50,000,000|43,561,088|34,484,352|stopped|
| AIO2.56M, LoRa |15.36|34.54|12,800,000|0|0|stopped|
| AIO2.56M, narrow |6.25|1.76|12,800,000|0|0|stopped|
| AIO2.56M, combined |15.50|35.82|12,800,000|79,872|0|stopped|

`source_error`, if observed, means an actual overflow and explicit incomplete stop; successful reporting is not successful complete capture. Analysis loss/skips imply possible misses. Actual RSP GUI stress images checked visibility of these states. Separate approximately10 second`rtl_test` runs ended at0ppm for2.4/2.56 MS/s and4ppm for3.2 MS/s, with small startup losses retained. None establish long-term or unknown USB continuity. HFconfiguration/noise is not calibrated sensitivity or payload qualification.

## Historical deployment

Source snapshot `414d7fd8b462d3e326ac89b01240291f4ec4f09f314a8082c3e625b90a9a146e`; install `/home/vicliu/.local/opt/amc/receiver-414d7fd8b462`.

- GUI SHA-256 `5d5759e9ccd127f431305bbacff9b47693b09c3faf9bdaa87db537a8fe989f90`
- CLI SHA-256 `a21bfb4dd17009b5a2ed9ff5b157a40b3ee683f7fcd7efb9ea73cb5775e0673b`

`current`, user commands and desktop entries matched actual process/path/hash. Old programs were removed without backups under the user's then-existing instruction; recordings remained. Saved RSP 10 MS/s/10 MHz and AIO2.56 MS/s/2.56 MHz preserved each center. Evidence: private `build/wideband-*.log`, `wideband-hardware-final.json`, `wideband-deployment.json`, remote `/home/vicliu/.cache/amc-wideband/`. This report was written after verification and excluded from the self-referential source fingerprint.
