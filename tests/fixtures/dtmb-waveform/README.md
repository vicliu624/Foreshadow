# Independent offline DTMB waveform

English · [简体中文](README.zh-CN.md)

This fixture generator starts with the redistributable synthetic two-program
TS in `../television-programs`, scrambles and BCH/LDPC encodes it, applies
constellation mapping, convolutional interleaving, system information,
frequency interleaving / inverse FFT for C3780 (direct symbols for C1), PN
headers, and an independent SRRC transmitter filter. It writes files only;
it does not connect to or transmit through radio hardware.

`gb20600-tables.json` contains numerical parameters extracted from
[GB 20600-2006](https://upload.wikimedia.org/wikipedia/commons/2/2c/GB_20600%E2%80%942006.pdf),
PDF SHA-256 `3dd373d098e0204ef17b77384f8685a4026f9ddc09522d7832a981267a4dca8f`:
Appendix A generator polynomials, Appendix B sparse parity-check circulants,
Appendix C NR mappings, Appendices D/E PN phase vectors, Appendix F frequency
permutation, and Appendix G system-information vectors. It contains no scan
images or broadcast media. The extraction checks dimensions, indices,
uniqueness and complete tables. Reproduce with `extract_tables.py PDF`; only
that maintenance command needs PyMuPDF. The output hash is
`f200fad70877d1de79450b8eaa388fdc47c9bb88fad3211c8f07f132b95d6013`.

The 127-bit hexadecimal generator values are loaded as polynomial
coefficients, with explicit low-to-high serial coordinates and rightward
circulant row shifts. `dtmb_fec_finite_tail` verifies the resulting encoder
against the independently extracted Appendix B matrices, for every information
block and cyclic boundary. It then checks all three rates through the actual
native decoder. This prevents a silently reversed generator representation
or a receiver-only round trip from serving as the oracle. Full-waveform
generation additionally compares one FFT convolution codeword against exact
integer XOR/rotation arithmetic. No receiver implementation is imported by
the Python transmitter.

The generator uses a primed transmitter interleaver before the recording
starts. The receiver still starts with an empty deinterleaver and must acquire
the waveform and discard its actual latency. Testing an unprimed transmitter's
zero-filled startup as a steady broadcast would produce invalid power and
normalization conditions. Payload comparison uses every original non-null
media packet, in order, rather than merely a recognizable programme name.

PN595 follows section 4.6.2.2, figure 12: D1 through D9 are zero, D10 is one,
and D10 is output before the first clock. Its first chips are `80249A6B`.
PN420 and PN945 can use the full published phase schedules. By default the
generated recording uses phase zero, which the standard also permits when
frame-number signalling is not required. `--phase-cycle` exercises the schedules.

An example from the repository root (Python 3 and NumPy are needed only for
offline generation/qualification):

```sh
python3 tests/fixtures/dtmb-waveform/generate.py \
  --pn 945 --si 22 --frames 1800 \
  --output '/tmp/电视验收 📡/programs.sigmf-data'
python3 tests/fixtures/dtmb-waveform/verify.py \
  --decoder /path/to/build/dtmb_receiver_test \
  --output /path/to/qualification --cases all --phase-cycle
```

`generate.py` also writes SigMF metadata, generator facts and the source TS.
Its default output is CF32LE at 15.12 MS/s, SRRC alpha 0.05, two samples per
symbol and a 64-symbol filter span. `--symbol-spaced` is a diagnostic input
before pulse shaping and cannot establish the complete RF chain's acceptance.

`verify.py --cases all` checks all 66 declared C3780 PN/profile combinations
and the six declared PN595 / 4QAM C1 profiles. Each result records actual
discovered mode, programme IDs, exact packet comparison, CRC, continuity,
FEC syndrome / BCH status and timing facts. A failed or incomplete case makes
the command fail; it is not silently skipped. IQ files are removed only from
the command's newly created case directories unless `--keep-iq` is requested.
The JSON proof and decoder logs remain. Normal CTest has no Python/NumPy
runtime dependency.

This is a clean synthetic receive-chain qualification. It does not establish
desktop presentation, audio-device output, live throughput, changing clocks,
multipath performance, AVS+/DRA media support or local over-the-air reception.
Those have separate inputs and acceptance evidence. Generated synthetic media
remain CC0 as documented in `../television-programs`; the generator code uses
the project's license. The external recorded reference remains a separate
positive control and must not be replaced by this fixture.

The original matrix keeps the configured SI vector constant. Add
`--superframe-markers` to `generate.py` or `verify.py` to replace each first
superframe's SI with the normative alternating odd/even vectors 1 and 2.
This is separate from cycling the PN headers; neither option changes the
payload modulation/coding profile. Mid-stream profile changes and reserved
SI remain separate qualification cases.

`verify_formats.py --decoder BUILD/dtmb_receiver_test --output DIRECTORY`
additionally checks the complete PN945/SI22 waveform at 10 MS/s, with first
superframe markers, in both CI16LE and CU8. It uses independent SciPy
`resample_poly(125,189)` and explicit file quantization (CI16 scale 32768,
CU8 scale 128 / midpoint 127.5). SciPy is only needed for that offline
qualification command. Generated SigMF files and the proof include hashes
and clipping counts; no physical receiver result is implied.
