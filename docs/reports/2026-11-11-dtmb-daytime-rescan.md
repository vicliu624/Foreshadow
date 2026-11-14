# DTMB daytime rescan and candidate review

English · [简体中文](2026-11-11-DTMB白天复扫.md)

Historical survey on the morning of 11 November 2026, using RSPdx-R2 on 192.168.31.42. The ordinary desktop still ran AMS 0.2.9. Independent CLI capture and offline inspection did not replace it. The [earlier detection report](2026-11-11-dtmb-detection.md) remains evidence for its own revision.

## Channel basis and valid captures

The [NRTA receiving guide](https://www.nrta.gov.cn/module/download/downfile.jsp?classid=0&filename=324831b10cc44c279f38de1637afe01a.pdf) describes 8 MHz terrestrial channels, including low VHF, 167–223 MHz, 470–566 MHz and the former 606–798 MHz allocation. With the [MIIT 700 MHz adjustment](https://wap.miit.gov.cn/zwgk/zcwj/wjfb/tz/art/2020/art_e5389a57ece640b4bd1b69dd74cff8a7.html), this survey prioritized UHF below 702 MHz and VHF III; the old 702–798 MHz table was not assumed to represent generally active DTMB channels.

| Range | Centers in this survey |
|---|---|
| UHF 470–566 MHz |474–562 MHz, 8 MHz steps, 12 centers|
| UHF 606–702 MHz |610–698 MHz, 8 MHz steps, 12 centers|
| VHF III 167–223 MHz |171–219 MHz, 8 MHz steps, 7 centers|

A national grid does not prove a particular local transmitter. Location, antenna type and installation were unconfirmed. Each center was captured on Antenna A and B after 250 ms settling, for 1.2 seconds. Readback confirmed 10 MS/s, 8 MHz IF and selected center/port. All 62 files contained 12,000,000 CI16 pairs/48,000,000 bytes: 744,000,000 pairs/2,976,000,000 bytes total. Capture/scan exited normally, with zero reported source rejection/timeouts, without a hardware sequence counter.

AMS inspected 558 windows and emitted zero DTMB events. This does not establish absence of DTMB in the band. The pinned native frontend (`ningzichun/dtmb-sdr`, `2cc94c012128d6fa3e8d6de456b3bb57e51990a9`) independently inspected the first 0.5 seconds of each file after floating-point resampling, trying PN420/595/945. Of 186 checks, 167 rejected insufficient repeated PN; 19 completed frontend work but all failed automatic SI confirmation. Exit code zero was not treated as SI lock.

## Candidates and unsuccessful decoding

554 and 562 MHz showed PN945 periodicity on both ports; added PN superframe-cycle tracking reported cycle lock. A 64-frame investigation and 24 further timing/equalizer settings still did not pass SI thresholds.

| Center | A PN metric | B PN metric | Best SI hypothesis | Confirmed SI | Valid TS |
|---|---:|---:|---:|---|---:|
|554 MHz|0.833079|0.701085|16|No|0 packets|
|562 MHz|0.864324|0.852550|14|No|0 packets|

These PN correlations are neither SNR nor success probability. SI16/14 are hypotheses. Full deinterleaving, LDPC/BCH, descrambling and SI-based phase-normalization checks recovered no TS. FEC/nonempty-output gates remained enabled; thresholds were not lowered to manufacture a positive.

The captures are valuable reproducible reception candidates, without program-stream or television-playback success. AMS did not report them. Detector misses, input conditions and equalization/tracking remain competing explanations; “no local signal” or “insufficient CPU” is not an established sole cause.

## Independent positive control

The same host/frontend confirmed PN945/C3780/SI22 on the [public remodulated reference](https://raw.githubusercontent.com/wiki/ningzichun/dtmb-sdr/Samples.md): SI metric 0.984794, next-best margin 0.222152. ZIP SHA-256 `1982a40508112e32397b6b06c3f41f63ff3f17336ccd6dfd4b05a6752ade4853`. This is an inspection-chain control, without antenna reception in this survey.

Full floating-point decoding on the development host recovered 6651 TS packets. The new generic parser found program 35, CGTN/RTHK, PMT350, PCR/H.264 351 and two AC-3 tracks, without section framing/CRC/continuity errors. Independent media decoding produced five video frames, retaining missing-reference/incomplete-audio diagnostics. That did not qualify the desktop player or complete AV synchronization.

New sample-writing/program-table contracts passed; the local 55-test suite had no failure. Ten upstream tests also passed with NDEBUG actually removed, separately from assertion-disabled Release tests. At this development date desktop playback, live AV output, AVS+/DRA, cross-platform real packages and deployment were pending; later reports record their completion.

Private evidence: `~/.cache/amc-toolboxes/dtmb-survey/daytime-20261111-085821/` on the target and ignored `build/dtmb-daytime-proof.json` locally. Original captures, empty decoded outputs and failure logs were retained.
