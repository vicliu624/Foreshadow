# DTMB structure detection and on-air survey acceptance

English · [简体中文](2026-11-11-DTMB识别与实收扫描验收.md)

Historical report, 11 November 2026, implementation `d58015a51a26dc599200ded8ebc7e90a2b447df9`. Structure detection, field reception, sustained processing and release validation are separate; compatible configuration does not establish reception or television decoding.

## Structure and extension proof

The provider checks GB 20600-2006 PN420/595/945 headers and 3780-symbol bodies. Algorithms, requirements, descriptors, event features and both languages belong to DTMB; generic menus/assessment/events gained no DTMB-ID branches. Snapshot protocol v2 adds provider-declared periodic observation, with independent task slots and separate busy/completed/pending/processing byte counts. Independent-provider tests cover constant input, arbitrary origins, gaps, overload and one deliberately suspended policy while another continues.

Independent standard vectors check polynomial direction, register output, periods and PN595 truncation. A separate transmitter fixture generates all three headers from standard register diagrams and initial states, with single-carrier/random 3780-point multicarrier bodies, 8/9.6/10/10.666/20 MS/s, carrier offset, fractional timing, 6 dB SNR, multipath and CU8/CI16/CF32. Two complete channels of the same PN at different centers can emit simultaneously. Noise, CW, FM, CP-OFDM, unrelated/continuous PN, a single valid header and wrong frame spacing remain negative. These structural fixtures do not include complete SI/FEC/program decoding.

The [independent remodulated reference](https://raw.githubusercontent.com/wiki/ningzichun/dtmb-sdr/Samples.md) declares 16 MS/s, PN945, profile22. ZIP SHA-256 `1982a40508112e32397b6b06c3f41f63ff3f17336ccd6dfd4b05a6752ade4853`. Exact declared-CI 8→CI16 conversion gave CLI/actual GUI PN945, 18 frames, approximately0.911correlation and first inspected frame at source sample10000. This is not local RF reception or complete profile22 decoding at this stage.

High-rate file scanning also stopped using a scan window as a read block: reads now obey 4 MiB while overlap/absolute coordinates remain intact. 16 MS/s CI16/CF32 regressions passed.

## Field survey without a confirmed positive

RSPdx-R2 used 10 MS/s, automatic gain, actual8 MHzIF,150mssettling then50ms/500,000 CI16 pairs per capture; detection inspected continuous12mswindows.105valid captures covered60centers: 69onA/36onB. A covered all points; B supplemented main UHF/VHF and570–602 MHz. No structure-confirmed event appeared. Reported rejection/timeouts were zero, without a hardware counter.

| Category | Centers MHz |
|---|---|
| Main UHF |474–562 and610–698, 8 MHz steps|
| VHF |171–219, 8 MHz steps|
| Low VHF supplement |52.5,60.5,68.5,80|
| Additional exploration |570–602 and706–858, 8 MHz steps|

Sources: [NRTA guide](https://www.nrta.gov.cn/module/download/downfile.jsp?classid=0&filename=324831b10cc44c279f38de1637afe01a.pdf), [channel grid](https://www.nrta.gov.cn/module/download/downfile.jsp?classid=0&filename=6d7f314d26524eb896d2473fbcd65f58.pdf), [700 MHz adjustment](https://wap.miit.gov.cn/zwgk/zcwj/wjfb/tz/art/2020/art_e5389a57ece640b4bd1b69dd74cff8a7.html). Exploratory frequencies are not a claim of current local DTMB use/authorization; cellular OFDM must not be called DTMB.

An early supplemental scan concurrent with native tests had16device-unavailable attempts. After regression, that25-center group was recaptured successfully. Both failures/successes remain; unavailable hardware is not no signal. Unconfirmed antenna/installation/coverage prevents an absence-of-local-transmission conclusion.

## Sustained input and stage accounting

Ryzen9 3900X/24 logical processors, GUI 210 seconds, actual207.985 seconds/2,079,850,496 pairs.100 MHz/10 MHz search, LoRa/narrow/snapshot detection and muted FM ran together. Broadcast FM input tested load/DTMB negatives, not sustained DTMB reception.

| Stage | Result |
|---|---|
| Acquisition/recording/main/narrow |Each processed2,079,850,496 pairs; no reported input rejection/timeouts/mainloss/narrowskips/gaps|
| Energy snapshots |129triggered/129completed/0busy skips|
| Periodic snapshots |208/208/0|
| Final snapshot payload |0pending/0processing bytes|
| FM |No input rejection/gaps/PCM latency loss; 5output underruns recovered|

The earlier single-slot candidate skipped101busy windows. Policy isolation completed337windows in the final comparison; skipped analysis is not hardware loss.32,255short reads ranged448–16,384 pairs, accounted by actual payload. Whole-machine CPU and equivalent cores do not prove capacity of every serial task; 210 seconds is not a long-term guarantee.

## Release and deployment

Local53 checks passed. Native Debian had no failures/3mock-driver discovery skips; real RSP was checked separately. French-only resources extended DTMB descriptions with per-keyEnglishfallback. [Self-hosted CI 42862946658](https://github.com/vicliu624/AMC/actions/runs/42862946658) and [formal six-platform42862950520](https://github.com/vicliu624/AMC/actions/runs/42862950520) passed for [0.2.9](https://github.com/vicliu624/AMC/releases/tag/v0.2.9).

Debian x64/ARM64, Ubuntu x64/ARM64 and macOS ARM64 ran53 checks; Windows 51; Ubuntu x64also7Sub-GHz checks. All actual archives/toolbox transactions passed. Hosted Linux/Windows skipped device audio without outputs; macOS executed it. Actual Unicode/non-BMPapplication/modules/independent-toolbox checks passed. Release keys remained outside Runner/repository.

30assets (24packages,5archives, catalog) matched uploaded size/hash. Catalog1794332352merged104records. An actual Debian archive extracted into `DTMB 发布 📡` discovered application/two receivers; GUI SHA-256 `3c0e330eca69edae2f20aa55e5074ded8b80af8f8ed438a5cae8a14c000858c5` matched the formal application package.

That release binary ran another210 seconds: 2,084,880,384 pairs processed by acquisition/recording/main/narrow, zero rejection/skips/gaps. Nonblocking exported status showed periodic209/209, energy126/127completed with one still processing, zero busy skips; status did not wait for unrelated DSP merely to report zero. FM had no input/PCM loss and3recovered underruns.

Standard signed transactions updated application/RTL/SDRplay/powerreport to0.2.9; activation returned `applied`. Actual GUI hash/stable CLI/desktop matched; original config and systemproxy remained, no persistent test arguments/development tunnel. Stable-launcher reference scan again gave PN945/18 frames/0.911/sample10000. Only intendedEnglish/Chinese production resources shipped. Private RF/per-frequency logs stayed in target cache.

At this report date, local DTMB positive, SI/constellation/FEC, TS/program/AVrecovery, human listening and longer lossless operation were unverified. Later television reports add software/media evidence; local antenna-to-program success remains unverified.
