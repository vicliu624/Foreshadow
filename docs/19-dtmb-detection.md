# DTMB structure detection

English · [简体中文](19-DTMB识别规格.md)

## Evidence and ownership

Detect frame structure from [GB 20600-2006](https://openstd.samr.gov.cn/bzgk/std/newGbInfo?hcno=41F48525779777A75F0898B87E9BD24E), independently validating PN420/PN595/PN945 and repeated-frame timing for single/multicarrier bodies. Results expose header mode, correlations, checked frame count, estimated center and source positions. Frequency, energy plateaus and ordinary OFDM CP cannot substitute for DTMB evidence.

This detector's capability is waveform/frame recognition. Constellation/rate/program names/media require separate system-information, equalization, FEC and TS evidence; the later television implementation is specified separately in [20](20-dtmb-television.md). The detector's stage boundary does not mean television is currently unimplemented.

DTMB provider owns algorithms/parameters/input-coverage/assessment/features/resources. Registration and CMake are assembly points. Generic main window/menu/assessment/event UI must not route by DTMB ID or embed multilingual tables.

## Periodic observation

Continuous television need not produce an energy burst. `SnapshotExtension` v2 explicitly declares observation strategy: legacy energy trigger or periodic observation, with window duration, interval and minimum rate. Generic bounded collection dispatches only matching strategies. Independent test providers verify this, without a DTMB-specific scheduler branch.

Strategies have independent bounded task slots; a slow one cannot consume another's opportunity. Publish per-strategy triggered/completed/busy-skipped/pending/in-flight bytes plus totals. Overload skips are software analysis evidence, not hardware sample loss; acquisition never waits.

Reliable coverage targets a full Chinese 8 MHz channel. Sample spectrum, actual IF, search range and edge truncation are distinct. Insufficient input must not show guaranteed detection. Codec-normalized I/Q retains original clock and arbitrary nonzero source origin.

## Acceptance

1. Independent appendix D/E PN vectors and figure-12 registers verify direction/polarity/period/truncation. Positives cover all modes/bodies, fractional ratios, timing/CFO/noise/multipath/encodings.
2. Negatives include noise/CW/FM/ordinary CP-OFDM/other PN, a single legal header, wrong intervals and gaps. One isolated correlation is not stable DTMB.
3. Sustained constant input produces results without energy jumps; primary status does not wait on DSP. Bound input/status memory; publish load/skips; reuse replay/CLI/menu/assessment/event/fallback protocols.
4. Scan the real RSP environment against public channel candidates, retaining raw short input/provenance. Missing local signal/antenna conditions are unverified field reception, not replaced by synthetic acceptance.
5. Run architecture/contracts/regression and Unicode/non-BMP archives before controlled release/deployment verification.

Candidate grids: UHF 13–24 at 474–562 MHz/8 MHz and 25–36 at 610–698 MHz/8 MHz, supplemented by VHF 6–12. [Public grid](https://www.nrta.gov.cn/module/download/downfile.jsp?classid=0&filename=6d7f314d26524eb896d2473fbcd65f58.pdf) is not local occupancy. [MIIT 2020 notice](https://wap.miit.gov.cn/zwgk/zcwj/wjfb/tz/art/2020/art_e5389a57ece640b4bd1b69dd74cff8a7.html) reallocates 702–798 MHz to mobile communication; mobile OFDM there is not automatically DTMB. Standard, allocation and measured occupancy are different sources.
