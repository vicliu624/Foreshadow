# Changes applied to pinned dtmb-sdr sources

English · [简体中文](README.zh-CN.md)

The archive and commit remain pinned in `cmake/DtmbReceiver.cmake`.
`cmake/PatchDtmbSource.cmake` makes build-tree copies and applies each reviewed
literal replacement exactly once. Missing or ambiguous context fails the
configuration. The downloaded source tree is not modified. Original MIT
notices continue to apply.

* `pn595.json`: correct the initial output chips to GB 20600-2006 figure 12
  (D10=1, D1..D9=0, read D10 before clocking). The original dependency uses
  the opposite end for the first one. That can appear as a small header
  alignment error but fails actual PN595 C3780 payload decoding. The separate
  AMC literal-vector check and complete independently generated waveform
  exercise the corrected implementation.
* `pn595-test.json`: correct the dependency's associated literal vector to
  the same normative register diagram. This does not disable its PN tests
  or their assertions; AMC also checks an independent literal prefix.
* `fec-eof.json`: a recording ending between complete FEC groups drains all
  prior groups and reports `trailing_fec_llrs`. Missing LLRs are discarded,
  never padded. Complete groups still require LDPC/BCH checks and the existing
  nonempty-output gate. Partial floats, nonfinite tails and I/O failures
  remain errors. Independent transmitter vectors test NR, 32QAM and all
  three code rates, exact output bytes and corrupt-tail rejection.

These are private helper implementation changes. Stable GUI, capture,
programme catalog, sample-codec and media consumers contain no new mode or
vendor branches as a result. New dependency versions must reapply and verify
the changes explicitly instead of silently reusing the same text patch.
