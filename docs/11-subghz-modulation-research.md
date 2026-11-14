# Sub-GHz modulation research

English · [简体中文](11-Sub-GHz调制识别与物业信号调查.md)

This is a historical engineering record from 5–6 November 2026. Its experiments explain the origin of the narrowband, CSS, PSK and OFDM detectors. Statements about unfinished integration describe that date; current capabilities and acceptance boundaries are defined in specifications 13–26 and the current README. The original observations, failures and performance limits remain evidence, rather than present-day product promises.

## Scope and passive observations

The work targeted Sub-GHz metering, building sensors, remotes and similar signals. It did not cover 125 kHz near-field RFID through this receive chain, 13.56 MHz NFC, or 2.4 GHz Bluetooth, Zigbee and Wi-Fi. Modulation resemblance alone does not identify a device or application.

At 22:33 on 5 November, two eight-second recordings at 478.5 and 478.7 MHz used 1.75 MS/s and approximately 38.6 dB gain, giving 14 million complex samples each. The private evidence directory was `meter-css-20261105-223329`. An 8192-point FFT and eight-segment median spectrum were used for inspection.

| Input | First recording | Second recording |
|---|---:|---:|
| Raw candidate count | 4 | 5 |
| Candidates after the diagnostic notch | 4 | 6 |
| Confirmed LoRa structures | 0 | 0 |

The DC component moved with the receiver center by 200 kHz. Lines near 478.127, 479.063 and 479.277 MHz remained unidentified. A diagnostic copy removed approximately ±800 Hz around DC in CU8 samples; the original recordings were retained. Sixteen seconds of passive input cannot establish deployment coverage or absence of traffic.

## Live integration and controlled transmissions

On 6 November, the live FSK implementation processed 525 million complex samples over 300.70 seconds without reported LoRa queue loss, input gaps or result loss. The narrowband pipeline inspected 16,017 blocks and produced six results. Process CPU was about 1.96 cores in the shorter run and 2.09 cores in the longer run. Controlled Pager GFSK and LoRa trials each produced 3/3 associations, including 1/1 checks with the installed application.

After adding OOK/ASK, another 300.70-second, 525-million-sample run inspected 16,023 narrowband blocks with no skipped blocks, at about 2.08 CPU cores. Controlled OOK, ASK, GFSK and LoRa examples were each observed once. Only the LoRa detector triggered the configured event recording policy. These measurements describe software accounting for those runs; they do not prove the receiver hardware never lost samples.

The GFSK control used 478.625 MHz, nominal −9 dBm, 32 bytes, 2400 symbols/s and ±4.8 kHz deviation, with a two-second recording. Blind ARM and Windows analysis estimated a center of 478.625183 MHz and approximately 8.35 kHz between frequency states. This was modulation evidence, without payload recovery. Earlier paragraphs in the original chronology saying that GFSK was not yet integrated refer to the earlier prototype, before the live tests above.

## Offline prototype and negative controls

`tools/classify_subghz.py` was the historical Python research entry point. It accepted `--input`, `--channel-hz` and `--width-hz 40000`, or a bounded `--scan` of the strongest channels (default eight, maximum sixteen). CU8 and CI16 were supported, with a 4,194,304-complex-sample limit. Edge signals, dense occupancy, strong interference and continuous signals remained conservative classification boundaries. The current native detector implementations supersede this script as the production path.

Known Pluto-generated inputs, without RF transmission, gave 3/3 OOK, ASK and 2-FSK classifications. Noise and a continuous carrier remained unknown; CSS did not enter the narrowband classes. A concurrent 3 MS/s fixture contained three bursts of each OOK, 2-FSK and 4-FSK, plus a continuous carrier and a short tone. Four channels were inspected; all three bursts of each intended class were found, the short tone remained unknown and the continuous carrier was not counted as a burst. A 4-FSK result required four stable, approximately equally spaced states.

Injection into actual background recordings used two bursts each of OOK and FSK at three amplitudes. Those controls passed, while unchanged background and Pager LoRa recordings did not acquire those classifications. This finite control set is not a field false-positive-rate estimate.

PSK research combined phase-power carrier-offset estimation with a bounded symbol-rate/phase search and constellation-state checks. Candidate rates were 1200, 2400, 4800, 9600 and 19200 symbols/s. Tests at 1.75 MS/s, two carrier locations and three noisy shaped examples per setting passed. The candidate search was not a generally reliable blind symbol-rate estimator. Later trained PSK reception and EVM are specified separately in specification 25.

`--start-seconds 5 --duration-seconds 2` selected a two-second region of an eight-second recording while retaining original source time. Pager LoRa controls were detected 3/3 without OOK/ASK/FSK/PSK claims. Chopped portions of BW 250/SF11 traffic could remain unknown; they must not become separate device counts.

## LoRa association and generic CSS

The optional `--lora-exe` integration used the same CU8 input structure as the native LoRa detector. Only unknown fragments inside the unique matched time/frequency region were associated with a LoRa structure. Energy following the SFD was not decoded payload evidence. Approximately fifty fragments could merge into one bandwidth-activity region, while a coincident separate OOK burst remained distinct.

Template CSS candidates required at least six repeated chirps and covered BW 125/250/500 kHz and SF7–12. Independent fixtures contained three generic CSS examples and negative controls; a real LoRa recording remained a separate structural result. `--blind-css` instead measured repeated ridges, direction, width and period without requiring those LoRa templates. Eight examples each at approximately 187/213 kHz and 1.7/2.3 ms passed an 8% slope tolerance. Continuous carriers, FSK and isolated sweeps did not qualify. Weak or concurrent signals beyond the dominant ridge were not accepted coverage.

Multiscale windows of 64, 256 and 1024 samples covered fast chirps near 0.39 ms and slow chirps near 6.5 ms, with eight examples each. Pager controls contained three independent preambles of ten upchirps. Associated post-SFD activity still did not prove packet bytes. Ten overlapping two-second windows from two passive eight-second recordings, with 50 ms overlap, found no repeated CSS; this conclusion applies only to those inputs.

`--scan-all` used one-second ownership intervals with 250 ms context and emitted JSON lines. Ownership by original start time prevented overlap duplicates. `--lora-notch` allowed at most two narrow diagnostic notches, on copies, keeping filtered and original results separate.

## OFDM candidate evidence and boundary handling

The early `--ofdm` check examined cyclic-prefix ratios 1/4, 1/8, 1/16 and 1/32 in bursts, comparing repeated CP structure across symbols with non-CP regions. Noise, carrier offset and echo controls for two OFDM patterns passed; non-CP signals and continuous carriers did not, and Pager traffic did not produce OFDM results. This was a structural candidate detector, not the later synchronized, trained receiver.

A batch with four OOK bursts, including two crossing window boundaries, reported each once. A LoRa boundary fixture had preamble start 0.997632 s and SFD start 1.001136 s and was also reported once. An eight-second scan emitted eight JSON rows. Overlap context and result ownership are therefore part of the evidence, not merely plotting choices.

## Performance experiments and rejected changes

Alternating old/new/new/old measurements kept outputs fixed before comparing cost:

| Experiment | Result | Interpretation |
|---|---|---|
| Windows, two-second 4-FSK input | Wall time 1.77 → 0.70 s; eight events unchanged | Improvement for this path; excludes CSS/LoRa and does not prove ARM throughput |
| ARM implementation comparison | CPU 24.42 → 7.77 s; wall 26.79 → 8.61 s | Still slower than real time |
| Concurrent ARM LoRa observation | 124,583,936 samples, no reported loss; controlled BW 500/SF8 head covered | Bounded concurrent observation, not universal continuity |
| float32 and 384 stratified background frames | CPU 6.62 → 4.89 s; wall 7.11 → 5.39 s; RSS 356.3 → 349.1 MiB | Eight events unchanged; the last 32,768 LoRa samples were still pending |
| Block STFT, 128 frames | RSS 349.2 → 318.7 → 318.6 MiB; CPU 12.42 → 13.09, then 12.83 s | Memory reduction with a measured cost tradeoff; eight FSK/CSS events unchanged |
| Cached-background attempt | CPU 14.65 s versus 12.99 s | Withdrawn because it regressed |
| Native FFT-bin order | CPU 12.23 s; wall 13.29 s; RSS 318.6 MiB | Same outputs, still not a real-time guarantee |

These experiments motivated native bounded processing and separate stage accounting. They are historical measurements on specific hardware and inputs. They do not establish current whole-application latency, hardware losslessness, arbitrary signal recognition or payload decoding.
