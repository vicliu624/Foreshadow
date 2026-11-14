# DTMB program decoding: development-stage verification

English · [简体中文](2026-11-11-DTMB节目解码阶段验证.md)

This is the development record preceding the [0.2.10 formal release](2026-11-11-dtmb-television-release.md). Later frozen packages/deployment supersede earlier pending states, without erasing failures or changing the scope of short reference inputs. Requirements are in [specification 20](../20-dtmb-television.md).

## Native receiver and independent reference

`DtmbStreamDecoder` converts borrowed samples through the common codec into CF32, automatically discovers PN/SI and retains floating precision through resampling, synchronization/equalization, deinterleaving, LDPC/BCH and descrambling. Its bounded queue/worker isolates acquisition; gaps rebuild decoder/catalog state. Source generations, prefixes, counts and stage logs remain distinct. Pinned `dtmb-sdr` revision `2cc94c012128d6fa3e8d6de456b3bb57e51990a9` supplies native helpers/tables, without Python runtime. A real positive exposed a pipe-polling error: without a Qt event loop, every helper must be polled, not just the producer.

The [independent remodulated reference](https://raw.githubusercontent.com/wiki/ningzichun/dtmb-sdr/Samples.md) contains12,500,000 CI 8 pairs/25,000,000bytesat16 MS/s, **0.78125 seconds**. ZIP SHA-256 `1982a40508112e32397b6b06c3f41f63ff3f17336ccd6dfd4b05a6752ade4853`. Exact CI16 and resampled7.56 MS/s CF32 inputs discovered PN945/C3780/SI22 and recovered6651 TS packets/1,250,388 bytes, identical to independent CLI output: `368aafdef6d3f5eb64030f28e869307d080648dfbbe89122105fc083c167be45`.

Catalog: program 35, CGTN/RTHK, PMT350, PCR/H.264 351, two AC-3 tracks, clean section framing/CRC/continuity. This one reference did not qualify everyPN/C1profile or local antenna reception.

## Media providers and actual encoded inputs

Versioned, length-bounded IPC carries program/video/PCM/diagnostic/end messages. Desktop consumers do not import FFmpeg types. Codec providers declare acceptance/capabilities/creation/descriptions; an independent provider entered the same path and unknown tracks remained explicit.

Pinned FFmpeg `cd4e96eb314630788b7ff2f9820c395788c023e2` uses shared LGPL configuration with GPL/nonfree disabled. System FFmpeg 9.0.2`cavs`accepted onlyJiZhun, insufficient for AVS+. The separate MIT DRA core retains provenance/local fixes in [third-party notes](../../third_party/README.md).

The short recovered TS could produce6 H.264 frames/11nonzero AC-3 blocks, PCMRMSabout0.05825, retaining partial-GOPdiagnostics. Official [AVS+ input](https://fate-suite.ffmpeg.org/cavs/avsplus-1080i-testcard.ts),233684 bytes/SHA-256 `d999a97e1e9a6dcb6859c0194741bac180ae0c11102b145f4fd79b2bdb3f5fdf`, yielded five1920×1080 frames withAdler-32 `bc59eeee`,`490377c0`,`aca11851`,`70251b61`,`22be3dd4`, matching the pinnedFATEreference. It has no audio.

The [DRA stream](https://github.com/zuifengjianke/VlcAvsDra/blob/855fb73e70c55355cdc1ccc0374915372cb11630/others/dra.ts),32,000,000 bytes/SHA-256 `2c6e614c16f1b7f00dad924b7751a28a037a5b9d2107832b912b4f4c22eb7a3c`, used source data only, without project binaries/Androidlibraries.

| Program/service | Video PID/frames | DRA PID/48 kHz PCM frames |
|---|---|---|
|301/CCTV-1|512/291|650/599040|
|302/CCTV-2|513/285|660/598016|
|303/CCTV-4|514/264|670/595968|
|304/CCTV-10|515/297|680/600064|
|305/CCTV-12|516/286|690/600064|
|306/CCTV-13|517/297|700/598016|
|307/CCTV-14|518/278|710/599040|
|308/CCTV-15|519/273|720/599040|

Each selection emitted only its own PIDs and nonzero PCM, retaining two known diagnostics. Program301paced output took12.478s; pacing alone was not device/video synchronization acceptance.

Broadcast text supports explicitDVBmarkers and declared unmarked encoding, independently of UIlanguage. Tests coverISO6937accents/euro, GB2312/18030, UTF-8/16/non-BMP and invalid text. Native Windows API text checks did not substitute for a full Windows package. IPC tests cover byte order, negative PTS, arbitrary chunks, concatenation, budgets, truncation/version rejection. Initial64 tests passed; default pinned SDK installation and desktop increments later passed68.

## Desktop playback and timing failures

`MediaPlayback` and the television live provider added live/SigMF/TS, program/tracks, output/volume/mute/start/stop/export through existing registry/workspace contracts. Actual X11/Pulsechecks rendered the short reference, AVS+ five frames and DRA programs, at volume0. They did not prove human listening. SyntheticCC0two-program TS independently checks440/880Hzspectra and actual PID 257/258selection; merely relabelling combo boxes cannot pass.

The first zero/noise recovery experiment rediscovered a mode but lost the catalog. Changing only the reference start localized fractional timing sensitivity:

| Skipped ADCpairs | Before | After fractional acquisition |
|---|---|---|
|0|6651 packets/program 35|Same|
|1|No TS|6642/program 35|
|2|6642/program 35|Same|
|3|No TS|6642/program 35|

The1024-phase SRRC bank,16bounded decision candidates and persistent workers preserve old phase-zero behavior. Probe/stream share the same phase. Independent continuous-time tones validate the fractional action, block/thread invariance. A zero/noise sequence retained continuous coordinates but two lost-lock generations and65536no-signalpairs, later recovering valid TS/program 35. Continuous coordinates did not mean continuously decodable signal.

Private sampler protocolv3estimates clock from125mssuperframe correlation in millihertz. CW/noise remain unlocked. Independent noninteger-period truth checked5ppm;±50ppmconstant offsets both recovered6651 packets with identical TS hash. The0.78125sreference cannot establish long drift tracking.

## Complete independent waveform and dependency corrections

The [independent transmitter fixture](../../tests/fixtures/dtmb-waveform/README.md) starts withCC0 TS and implements scrambling/BCH/LDPC/mapping/interleaving/SI/I FFT/PN/SRRC without importing receive code. AppendixAencoding is checked against independently extractedAppendixBmatrices and exact XOR/rotation arithmetic.

PN595 needed the standard D10-before-clock initial output, prefix`80249A6B`; the upstream opposite-end vector allowedSIbut failed C3780 payload. Build-tree-only exact-context patches correct implementation and its literal test, without disabling assertions. A finiteNR/32QAM FEC group can span two frames: complete preceding groups drain, incompleteLLRsare counted/discarded without padding. Partialfloat/nonfinite/IOfailures and empty valid output still fail. Independent rate/mode/exact-byte/corrupt-tail tests passed.

Initial six modes then all recovered programs101/202/all593original media packets, zero CRC/continuity/LDPC/BCH errors. Full constant-SImatrix72/72covered66 C3780 plus6declared PN595/4QAMC1 cases,1200 frames/0.667–0.750seach, automatic acquisition from empty deinterleaver and standardPNphase schedules. Later first-superframeSI1/2matrix added another72/72. These are not midstream profile-change or sustained-broadcast qualification.

CompleteI/Qon31.42recovered11601 TS packets,10decoded/8initiallypresentedvideo frames and48384 PCM frames. Earlier1–2underruns remained recorded. Continuous24msaudio PTS with paced output nevertheless exposed175msarrival gaps; the new regression failed at174437 µs before correction. Independent timestamp-scheduled output, bounded encoded-message queues and separate cancellation/backpressure/drain handling corrected the same input toabout24.1ms, without UI/vendor-IDbranches or added buffering. Local72 tests passed.

Final muted device checks: completeI/Q0underruns/48384 PCM,8/10initial presentation and10/10after cached reselection; DRA 291decoded/290presented/599040 PCM/0underruns/1normal EOF drain/about15msclockdeviation, two input diagnostics retained; AVS+ 5presented/0decodeerrors. Late video remains counted. IndependentSciPy10 MS/s CI16/CU8 conversion with first-frame markers also recovered all593 packets/two programs/zero FEC/transporterrors, without clipping or RF claims.

An unsigned development archive,27,662,356 bytes/SHA-256 `92d5287f87b826e1b768fc8ccbd6dc8a7106c1895f36e7629a4a1bc9f1b30df0`, passed extracted Unicode application/helpers/resources/media checks, without substituting for formal frozen-source release acceptance.

Live subscription looped a short10 MS/s input for8s then overloaded: 113,548,576 pairs also reached an independent subscriber, maximumcallback2712 µs,2030 GUI event iterations,67,104,704bytepeakwithin67,108,864budget. TVanalysis rejected57,211,888 pairs explicitly, still produced video/nonzero PCM and stopped in8ms. These are TVanalysis gaps, not acquisition/other-subscriberloss; repeated short timestamps/waveform edges are not independent long broadcast.

Matrix and supplementary JSON proofs retain hashes/results. Final formal source`3612f78`, six-platform actual packages/signing/two-device deployment are recorded in the release report, including skips and externally blocked Mac timing. Local554/562 MHz candidates still did not yield valid programs. Long broadcasts, variable clocks, profile switching and hardware losslessness remain unverified. External recorded reference media are not distributed with source/application.
