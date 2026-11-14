# DTMB transport recovery and television playback

English · [简体中文](20-DTMB节目流与电视播放规格.md)

## Complete path

Continuous I/Q → synchronization/system information → equalization/deinterleaving/LDPC/BCH/descrambling → valid MPEG-TS → program/audio selection → native picture/sound. Real and recorded input are supported. Opening an external player, empty output or a program name alone does not complete playback.

Default acquisition discovers PN420/595/945 and system modulation/rate/interleaving with separate evidence/failures. Preserve input precision using shared codecs and declared private float protocols; do not requantize to eight bits. FEC/transport validation remains enabled. Unsupported/unlocked modes state their limits.

Clock/symbol acquisition must handle arbitrary recording starts, fractional symbol phase and reasonable source-clock error. Changing nominal rate without correct decision timing is insufficient. FEC, valid tables and media confirm the complete chain.

## Ownership

| Axis | Owner | Consumer |
|---|---|---|
| DTMB algorithms/modes/parameters | Receiver adapter | Generic I/Q/live host |
| TS framing/tables/CRC/continuity | Transport module | Television/media worker |
| Codecs/capability | Media module/providers | Generic media frames/television |
| Programs/tracks/output/state | Television provider | Workspace/event-action host |
| Pinned sources/data/notices | Dependency assembly | Build/package/generic installation |

LiveResearchProvider v2 adds independent leases while retaining v1 widgets/I/Q/events/diagnostics (specification 21). Generic host does not identify DTMB. Provider settings remain in its panel. Assembly is declared registry/CMake. Media IPC is versioned and length-bounded, not disguised as an old image/snapshot protocol. TS/media/DTMB own their respective rules, independently of receiver IDs.

Pin reused sources and preserve licenses/local changes. Verify actual enabled assertions/checks and the codec path entering the application, not upstream support claims.

## Interaction

Open through a DTMB event or View, choose live/recorded I/Q or TS and start. Provide stop/start, program/audio track, volume/mute, I/Q open, valid buffered TS export and receiver/playback state. Auto configuration is default; provider owns advanced validation/persistence.

Distinguish no source, incompatible input, buffering, PN discovery, system lock, FEC, awaiting tables/keyframe, playing, recovering and failed. Programs require valid PAT/PMT; names require SDT or an explicit unnamed fallback. Source gaps/switch/stop cannot leave stale playing state. Scrambled/unsupported tracks explain why. Real AVS+ and DRA paths need separate encoded references, not only MPEG-2 samples.

## Real-time and media

Acquisition only copies/offers bounded I/Q. Budget actual payload bytes for receiver, TS and media. Separate I/Q rejection, processing skips, TS continuity, codec errors, video lateness and audio underrun. Gaps clear/reacquire appropriate state, not fabricate a continuous TS. Publish real-time ratio/queues/losses rather than restricting hardware capability to hide CPU limits.

Use media timestamps for A/V sync, PTS wrap, discontinuities and program changes. Audio is fed outside GUI timers; GUI stalls cannot grow unbounded frames. Stop/close has deterministic completion; stale async results cannot revive stopped tools. Native media decoding runs in an isolated process; failure affects this optional tool with persistent provider diagnostics.

Output throttling must not wait for future video PTS before decoding subsequent audio. Output advances while input waits. Bound encoded message bytes separately for audio/video/control, counting the in-flight message. End cannot overtake decoded media. Clock-jump drops, producer backpressure and write failure remain separate. Real continuous-audio arrival and independent queue contracts verify scheduling rather than hiding defects with buffers or counters.

Shared-host timing uses three predetermined observations, retaining each maximum gap/PTS/queue evidence. Judge repeatable gaps by the median while each run still meets frame-count/integrity/byte-budget requirements. Do not rerun until a lucky pass or attribute a wall-clock stall to an algorithm without evidence.

Raw audio-arrival median threshold remains **120 ms**. Each run also measures an independent 24 ms timing control; Mac requests the same QoS as output. Only when all three have at least eight observations, QoS succeeds, and median-of-three control medians reaches **48 ms**, repeated host deadline misses qualify an otherwise excessive raw-media gap as an **external platform blocker** (CTest 77), not a latency pass. Decode/integrity/budget/drain failure still fails. Never subtract control time or claim it caused a particular audio gap.

Output snapshots expose deadline lateness, write durations/gaps per channel and receiver arrival time for comparison. Mac pthread QoS failure preserves error codes; QoS is not hard real-time. For short media with fewer than eight controls, collect at most two extra seconds of adjacent controls without rerunning media or changing original media thresholds; insufficient control still cannot qualify a platform skip.

CTest and extracted-package checks share the same outcome protocol. Allowed skip codes are declared per invocation; other nonzero outcomes fail. Generic runners do not infer error meanings from implementation/test IDs; a timing skip cannot bypass subsequent decode/UI/dependency checks. Formal logs retain all rounds, including passed/blocked data; actual device playback is separate evidence.

All visible text is provider-keyed with resource-only fallback tests. Native paths/UTF-8 apply to I/Q/TS/assets and Chinese/non-BMP archive validation. Codec-specific errors arrive through localized generic IPC fields, not UI codec-ID tables. Broadcast text encoding belongs to transport codecs: explicit encoding markers first, declared reception fallback otherwise, never desktop-language/code-page guessing.

## Completion criteria

1. Independent complete modulated truth passes the real receiver into verified TS, CRC/continuity/FEC, covering claimed PN/body/modulation/rate combinations and negatives.
2. Multi-program A/V survives full modulation/recovery and real provider UI selection, actual picture/nonzero sound/timestamp checks, plus actual AVS+/DRA references.
3. Exercise continuous/file/backpressure/gap/GUI-stall/restart/source-switch/disconnect/no-device/helper-failure while keeping acquisition/other consumers available.
4. Independent provider/resources and existing regression pass; real target packages include modules, full runtimes/resources/notices and standalone toolbox installation.
5. Attempt local DTMB. Missing signal/antenna means field reception unverified; standards truth remains valid but cannot be labelled local success. Software still needs real picture/sound closure.
6. Freeze source, validate packages, sign/publish/deploy through existing protocols and inspect actual running binaries/modules. Report completion only against evidence, distinguishing historical formal release from later review maintenance.
