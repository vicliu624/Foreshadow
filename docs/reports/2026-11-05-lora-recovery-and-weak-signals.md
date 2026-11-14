# LoRa recovery trials and weak-signal baseline

English · [简体中文](2026-11-05-LoRa恢复实测与弱信号基线.md)

Historical5–6 November 2026research. “Recovery” here concerns detecting structures from recordings/controlled transmissions; it did not establish received payload bytes or CRC. The uConsole used v3.1 at478.5 MHz/1.75 MS/s/38.6 dB gain/11 dB, blindly searching BW 125/250/500, SF7–12, both polarities. Pager`amc-pager-tx-0.2`used nominal−9 dBm.

## Baseline, association and candidate sequence

| Evidence | Recorded result |
|---|---|
| `edge-recheck-20261105-223823.json` |Six edge transmissions,6/6associated/full estimated frame/zero analysis-loss increment|
| `priority-recheck-20261105-223900.json` |Ten sent, nine completed observations; SSH255after tenth interrupted observation, remaining plan unexecuted|
| `association-recheck-20261105-224021.json` |After observer correction, high edge/SF12minimum payload/short preamble3/3|
| `v31-holdout-20261105.json` |Seed26110500,30 cases/group, BW 500/SF7:−4/−7/−10 dB SNR 29/6/0; wrongsync/SFD/noise/CW/periodicFSK0/30each|

The matrix constrained frame origins using pre-transmission source position, examined every event in a recording and retained ambiguity. Transient SSHretries repeated reads, never TX. Association used sample order/frequency/BW/SF/polarity, without device identity or CRC. Digital synthetic SNR is not antenna sensitivity. Independent seeds and actual failure waveforms must separate tuning from acceptance.

Of24misses in the original−7 dB group,13entered fine alignment, six passed weak seed without alignment and five passed neither. Joint preamble acceptance retained mean/coherence gates while allowing individual symbols3 dB lower. First/independent−7 dB counts rose6/30→7/30and8/40→9/40; other SNR groups were unchanged, five negative families70each remained zero. Windows 4 tests passed; ARM/RF were initially pending. Stage counters later located seven detections, eleven without refinement, seven stopping after downchirps and five after quarter endings.

Pluto-generated CSS/OOK/2-FSK used3 MS/s CI16,16384 sample alignment,+75 kHz offset and three bursts. Structure/quiet/envelope/frequency checks passed. IIOreadback showed sleep/TXLOpowerdown; SSHhost-key failure prevented the transmit workflow. No Pluto RF result was claimed.

Quarter-downchirp phase assistance extrapolated two complete downchirps, requiring phase/amplitude/minimum evidence and existing sync/direction/ending checks. First−7 dB 7/30→10/30; independent9/40unchanged; independent−4 dB 39/40→40/40. Five negative families70each stayed zero. ARM18/18controlled configurations and300.70s/525million/zero-loss checks passed; three selected weak replay positives and three negatives were checked. Installed GUI 1/1 passed. A long observation was started, without acceptance at that entry.

## Joint synchronization and retained miss

Two sync symbols could individually be2 dB below threshold only if their linear mean met the main threshold and grid/prominence checks passed. Identical hashed inputs yielded:

| Rate/seed/−7 dB | Previous | Joint sync |
|---|---:|---:|
|2 MS/s/26110500|10/30|15/30|
|2 MS/s/26117500|9/40|13/40|
|1.75 MS/s/26126000|7/30|11/30|

Thirteen additional detections, no previous positive loss;−4/−10groups unchanged. Five negative families100each had no structures. Seven matched ARMinputs recovered four selected misses with three negatives clean; three actual GUI captures replayed3/3on both versions. Restricting a weak seed peak to±2bins or extending10→12 symbols added no−7 dB gain and was withdrawn.

The earlier long observer was marked interrupted near540s before the new300sphysical test. Joint-sync matrix26/27had one BW 500/SF8center minimum-payload miss without per-case analysis loss. Three immediate repeats passed, without determining cause.300.69saccounted525million pairs in acquisition/analysis/recording, zero known loss/gaps. Installed GUI repeated that case3/3and a new observer started.

Three saved center recordings at32origins each passed96/96. Added Gaussian noise gave both versions24/24per level through CU8 componentσ28;σ32gave18/24, with the same six failures before refinement. The original missed transmission's raw I/Q was not saved, so this did not explain it.

## Dense short-symbol option

Global probe steps4/3/2/1detected18/18/20/24noisy inputs at14.35/15.92/21.41/33.91million FFT points. Step3both gained/lost two, showing origin sensitivity. Default-off`--dense-short-symbols 1`uses every symbol only for durations≤512 µs: 18/24→24/24at20.65millionpoints/about1.44×. It initially falsely accepted wrong-SFD seed26130020, so it was not deployed as the default.

A bounded ending-phase/amplitude check rejected continued downchirp beyond2.25 symbols. The fixed negative and six weak origins entered regression.−7 dB groups11/30→13/30and16/40→23/40, no previous correct losses, five negative families70each clean. ARMreproduced six0→1weak inputs and three negatives. Alternating off/on/on/off on an8spassive recording increased CPU 14.82→18.55s/about25%, FFT 255→383million; concurrent normal reception meant wall time was not isolated throughput.

On6November, an8sbackground plus one known weak burst at4s passed four paced14million-pairreplays without software loss. Default produced no recording; dense produced one exact-byte saved interval. Queuehigh-water off36/66 blocks versus on114/116, densewall11.00/11.24s. The ordinary concurrent receiver briefly lagged approximately2s then drained without new loss; this was not independent exclusive hardware qualification.

## Interleaved RF and noise controls

Fifteen shuffled−9 dBm transmissions at478.400185/478.500185/478.600185 MHz, BW 500/SF8,8 bytes/8preamble symbols gave5/5each/full estimated frames/no known loss. Median matched component−22.8/−22.8/−22.7 dBFS did not establish weaker center performance; the earlier miss remained unexplained.

Paired digital noise injection used each recording's preamble signal/background estimates and two origins. Both modes15/15unmodified and30/30at−4/−7 dB; at−10 dB default15/30versusdense20/30, without old positive loss. Maximum clipping fraction about0.145%. Reusing15recordings does not create30 independent field packets or an RF sensitivity measurement.

A bounded64-entry upchirp cache per template cleared when history origin changed; complete-spectrum tests retained full FFT. Same-input events stayed identical: densebusy34 matches, FFT 107.33→104.55million/about2.6%less, passiveabout0.5%less. Four local tests passed; candidate was not deployed.

## Narrow interference and optional notching

Fifteen real BW 500/SF8recordings each received seededCWor1200 symbol/sFSK,40–160 kHz from center, at0/−6/−12 dB SIR:

| Interference | Default and dense detections, each level out of15 |
|---|---|
|CW|15/15/14|
|2-FSK|15/14/12|

Ninety interference-only negatives were clean. Oracle-frequency notching restored15/15in both strongest groups. Estimating at most two lines from a known quiet prefix also restored them, without proving long blind estimation. A later whole-recording time-frequency occupancy/prominence estimator required neither TXtime nor frequency. Offline`--lora-notch`preserved original results/IQand listed only additional filtered matches:−12 dBCW14→15, FSK12→15;30 negatives clean. This did not alter normal monitoring.

Endpoint guards prevented cyclic convolution leaking a final strong carrier into the beginning. Two8spassive recordings had no LoRa/notch. An injected478.803 MHz carrier was estimated near478.802990 MHz and suppressed in all eight windows without LoRa false positives.

## Same-acquisition raw audit

`monitor --audit-iq-output`uses the existing acquisition and independent writer to preserve a continuous diagnostic copy, capped at8s/32 MiB, in a new directory separate from events. Ordinary long monitoring does not become continuous disk capture. Tests cover exact bytes without events, overwrite/nesting/infinite/overduration rejection and completion metadata.

Windows and ARMused the same hashed8s/14million-pairweak input: live default0, replay default0, replay dense1, with exact audit bytes and SFD checks. This was replay-path acceptance; the same-mode Pager RF capture remained pending at that entry. `tools/compare_lora_audit.py`checks completeness/count/rate/center and uses recorded detector settings, matching one-to-one by SFD/frequency while retaining missing/additional/ambiguous results. Known weak0/0/1and real edge1/1/1were checked. Pagerdiagnostics preserve receipts, restore normal configuration and never equate matching with payload validation.

## Overlap and fixed subband research

Two independent real BW 500/SF7recordings were shifted/overlapped at0/125/250/650 kHz separation, three timing and amplitude settings. Default/dense agreed. Even650 kHz nonoverlap gave both packets in only1/9wideband cases; closer overlap could miss/add structures. This did not establish cochannel collision separation.

Offline`--lora-split`retains original results, separately inspects fixed±350 kHz subwindows with200 kHz centersearch at1.75 MS/s, and deduplicates parameters/SFD/frequency. The first650 kHz group became9/9two-packetresults without extras. A second real pair remained limited: equal-strength12 cases produced both in six/one in six; 24 cases at±9 dB produced one each. Digital gain did not help; narrower transitions lost existing matches, so both changes were withdrawn.

Finally each signal was independently limited to560 kHz then combined over one background. All24strong/weakcases remained one packet wideband but two with subbands, no extras. This qualifies that band-isolated input set; earlier unfiltered-background failures remain. Independent Windows/Linux regressions include asymmetric band-limited cases. Private `build/rf-tests/lora-collision-*.json`, interference/noise/audit records retain per-input hashes and results. No general collision, sensitivity, payload or long-term-losslessness conclusion follows.
