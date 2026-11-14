# Conditional SFD tolerance and weak-signal candidates

English · [简体中文](0001-条件SFD容差与弱信号候选.md)

Date: 4 November 2026. This is a dated decision record, including later experimental appendices. Conditional SFD tolerance was implemented and checked on ARM and repeated RF trials. Weak-candidate work remained diagnostic; later v3 experiments below did not automatically change the deployed v2 acceptance state.

## Adopted decision

Continue to require six consecutive preamble chirps, the synchronization grid, two consistent downchirps, the quarter-symbol ending and its termination. Preamble/synchronization requirements remain unchanged. Only after the consecutive preamble exists may the downchirp peak-to-mean threshold be2 dB below the main threshold; local peak-prominence requirements remain unchanged.

Actual samples from the owned transmitter showed approximately13–14 dB preamble evidence,9.93 dB for the downchirps,8.14 dB for the quarter ending and−10.72 dB afterward. A uniform11 dB threshold missed it. Lowering the whole detector to9 dB admitted it but broadened the normal candidate entrance. Conditional tolerance changes only subsequent SFD evaluation, preserving the ordinary no-signal threshold and frequency coverage.

The regression segment and SHA-256 are in `tests/fixtures/lora-sf7-weak-sfd`. Tests do not provide transmitter frequency, bandwidth or spreading factor. Four Windows groups and five ARM groups passed. Noise, a tone, pure CSS and truncated/extended SFDs still did not become LoRa. Algorithm identity became `dechirp-fft-structure-v2`; results retain the effective downchirp threshold.

A240-second physical repeat test reported no software queue drops. At nominal−9 dBm, center BW 500/SF7 and the short-preamble case were3/3, low edge2/3 and high edge0/3. A high-edge0 dBm control was3/3. That control cannot replace the−9 dBm failures or rule out passband attenuation. Absolute receiver sensitivity remained uncalibrated.

## Remaining issues and diagnostic prototypes

High-edge samples had repeated preamble peaks, but some symbols were only8–12 dB and the global maximum sometimes jumped elsewhere. Lowering structural thresholds does not lower the13 dB seed threshold; continued failure does not demonstrate absence of signal.

Two existing dechirp FFTs half a symbol apart can test the expected repeated-upchirp displacement of approximately±BW/2, or±2^(SF−1)bins, followed by repetition one symbol apart. `tools/probe_half_phase_pairs.py` generated candidates in saved weak samples using known BW/SF. It did not validate full structure and was not a blind-detection score.

A three-point-consistency prototype retained candidates during the known high-edge transmit interval and reduced candidates elsewhere, but multiple short-preamble background candidates remained. Promotion to LoRa still requires raw-data preamble, synchronization and SFD checks, plus false-positive and CPU evaluation.

At the band edge, choosing the strongest local downchirp peak before checking its inferred center can let out-of-band interference displace a weaker geometrically valid peak. Applying the same center-range constraint earlier is a research hypothesis; quantization tolerance and final coverage must remain unchanged.

Any later change must retain simultaneous1 MHz center search, all three bandwidths, SF7–12 and both I/Q polarities, then rerun positive/negative controls, actual-recording regressions, repeated edge transmissions and sustained throughput. A candidate prototype is not a deployed capability.

## Synchronization references

On4November, the [EPFL LoRa PHY description](https://www.epfl.ch/labs/tcl/resources-and-sw/lora-phy/) and its linked [gr-lora_sdr synchronization implementation](https://github.com/tapparelj/gr-lora_sdr/blob/master/lib/frame_sync_impl.cc) were consulted. Their receive chain estimates/corrects sampling timing and carrier offset and uses buffered preamble evidence. That motivated examination of candidate windows and timing/carrier refinement rather than lowering every threshold together.

No code was ported and no GNU Radio runtime was introduced. Their performance is not an AIO2 measurement; unknown-frequency/multiple-BW/SF search and device resource limits still require independent qualification.

## v3 experiment: fine synchronization and joint SFD transition

Half-symbol three-point candidates only activate dense search. Final events undergo carrier/timing refinement across six preamble chirps, coherence and adjacent-frequency checks, two synchronization-symbol grid checks, two downchirps and a quarter-symbol ending. Activation alone cannot establish LoRa.

Actual high-edge recordings showed residual downchirp projection in a short window after data symbols began, so a5 dB end-drop test could miss the transition. The experiment jointly compared downchirp durations1.875,2,2.125,2.25,2.375,2.5 and3 symbols with subsequent unknown upchirp data. Only a2.25-symbol model beating alternatives qualified. It did not recover payload bytes or parse a protocol. Data-symbol candidates came from eight FFT peaks and adjacent positions, bounding work without enumerating a full payload. The0.5% score advantage was an empirical feature threshold without false-alarm-probability calibration.

Confirmation needed four symbols after the downchirp start plus refinement margin, updating `confirmed_at_sample`. Each attempt had a64Mi-point projection budget, at most48Mi for preamble refinement and the remainder for transition checks. Center-first ordering prevented long-SF hypotheses exhausting the budget before valid candidates arrived.

Four Windows tests passed, including the real high-edge segment and six incorrect durations at two offsets each. ARM and new RF transmissions were still pending at that appendix date. Native tests, blind BW/SF RF trials, queues and runtime load were required before deployment; one recovered recording was insufficient.
