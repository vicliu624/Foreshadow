# WSPR integration

English · [简体中文](wspr.zh-CN.md)

Numerical WSPR-2 decoding and the provider-owned realtime/recording panel are
implemented and verified in the ARM64 host; other platform acceptance remains outstanding. Physical HF reception
is not established by these software tests.
WSPR-2 has 162 four-tone symbols over 110.592 seconds; the existing sub-second
snapshot lifecycle cannot represent a complete frame.

`plugins/wspr/core` owns coding, synchronization scoring, deinterleaving and
message semantics. Long-input observation must preserve sample clocks and gaps,
budget actual payload and isolate DSP from acquisition. A new lifecycle requires
an explicit protocol upgrade, not merely larger snapshot limits. Realtime and
recording entry points must share the numerical decoder.

The Fano implementation is vendored unmodified from WSJT-X commit
`967c85a6119110788b0df3782cf796485b2cee14`. Original notices, GPL and file hashes
are retained in `plugins/wspr/vendor/wsjtx`. Its symbols remain module-private;
the upstream CLI, Fortran/OSD and global FFT/file state are not embedded.

Verified on ARM64 uConsole: independent convolutional coding, soft-error recovery,
invalid-budget rejection; five upstream encoder vectors through sync scoring,
deinterleaving, FEC and standard/prefix/suffix/hashed fields. Uniform unsynchronized
input is rejected; hashed callsigns remain unknown. This is not CRC/authentication.

The initial 375 S/s I/Q search recovered a reference waveform with a nonzero
start, frequency offset and 2 Hz linear drift, and rejected silence. The bounded
120-second/8-second-overlap history passed short-chunk, large-origin and gap
tests. The two-worker live input path recovered upstream reference signals
through every sample codec, retained source coordinates and initially cleared results
across a gap (the final retention rule below supersedes that prototype behavior). This verified the low-rate direct path, not yet the higher-rate
channelizer, weak-signal sensitivity or sustained device reception.

Eight standalone WSPR CTests subsequently passed after a consistent rebuild:
FEC, source fields/four-tone decisions, I/Q search, bounded history, live input,
downconversion, finite-input finish and recording analysis. Conversion checks
covered 12 kS/s actual samples and bounded multi-stage planning at 2 MS/s.
Finite-input tests decoded both a shorter-than-120-second complete observation
and the 12 kS/s path. Recording tests retained both sides of a gap, precise
source positions, Chinese/non-BMP paths and cancellation. Those initial eight
cases did not cover host integration, panel runtime or weak/multiple-signal
performance; subsequent standalone checks are recorded below.

Remaining acceptance: sustained high-rate
input and overload, physical HF reception, other native platforms and actual
distribution packages. The numerical decoder and localized realtime/recording
entry points are implemented and have the standalone evidence below.

Additional standalone evidence: English and Chinese panel tests completed live
reference reception, the generic recording action and stop. Their 800×600
screenshots were inspected. Actual 2 MS/s multi-stage conversion retained its
RF-frequency mapping and output clock. Seeded complex Gaussian-noise tests
decoded two stations 60 Hz apart at expected −19.7796 dB and one station at
−27.7319 dB, referenced to 2500 Hz by
`10 log10(A² / (2σ²) × 375 / 2500)`; `A=0.01`, with per-component `σ=0.0267`
or `0.0667`. The corresponding noise-only case produced no messages. These are
specific synthetic observations, not a general sensitivity/false-alarm guarantee.
No automatic WSPRnet upload or WSPR-15 decoding is supplied.

## Operation and resource boundaries

Open the WSPR reception and decoding panel through **View**. It uses the existing
live-research reception protocol, independently of short-snapshot detection.
Enter the RF observation center (not the USB dial frequency), lowest-tone offset
range and maximum frame drift. Reception needs a continuous complete frame;
the live path searches 120-second windows every 8 seconds. The panel also opens
I/Q recordings and participates in the generic recording actions.

Results show callsign or unresolved hash, locator, reported transmitter power,
lowest-tone RF frequency, frame drift, synchronization score and estimated source
start sample. The score is not calibrated SNR. Source positions include the
channelizer delay and search-hop quantization; the tooltip retains input segment
and sample rate. Unknown hashed callsigns are not guessed.

The final implementation retains decoded messages across input gaps and clock
resets, assigning separate input segments. Gaps discard incomplete observations;
already completed queued observations remain valid. Overlapping searches deduplicate
within each segment. Live history retains 256 messages, with total and expired
counts; recording analysis retains up to 4096 and reports overflow explicitly.

Acquisition conversion and message search run on separate workers. The raw queue
is limited to 16 MiB of owned vector capacity plus block objects and 240 seconds
of input, with blocks no larger than 1 MiB. Empty callbacks consume no queue
entries, while explicit continuity markers survive to the next nonempty block.
Two 120-second normalized observations may queue (720000 payload bytes); the
in-flight input and observation have separate counters. FFT workspace is bounded
to 8 MiB. These are module budgets, not total process RSS. Input rejection,
observation rejection and source gaps are reported separately. Recording analysis
uses backpressure. Stopping joins workers on the owning thread and ignores late
session callbacks.

## Host acceptance, 2026-11-24

The complete GUI and WSPR tests were rebuilt together on the ARM64 Debian
uConsole. All 16 selected CTests passed: 12 WSPR tests (including English,
Chinese and missing-language fallback), existing recovery, architecture,
extension behavior and live-research extension contracts. After the visual
column-width correction, all three panel tests passed again. Native X11 panel
screenshots were inspected at 800×600. The main application's provider diagnostics
reported `wspr-live` available under protocol v3, and its View menu exposed the
provider without a main-window special case. No existing installation was replaced.

`wspr_live_probe` supplied 140 seconds of paced 12 kS/s CF32 reference input:
1680000 pairs processed, zero input/observation rejection, zero discontinuities,
four analyzed windows, one decoded/retained message. Elapsed time including final
drain was 143.628 seconds. The observed queue charge peaked at 24064 bytes and
the maximum sampled snapshot call took 55.739 microseconds. This is one synthetic
run on this host, not proof of hardware acquisition losslessness or an indefinite
performance guarantee. The separate 2 MS/s conversion test covers four seconds.

Windows/macOS native execution, physical HF reception and real release packages
remain unverified. This development change does not publish or install an online
update, and does not reuse the existing 0.2.13 release as a WSPR release.
