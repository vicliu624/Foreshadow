# Continuous monitoring and event storage

English · [简体中文](08-长期监听与事件存储.md)

Current operation supports monitoring until stopped with bounded RAM and disk use. No completed general 24-hour lossless hardware acceptance is established. The older observation starts and failures below are historical evidence, not currently running jobs.

## Defaults and units

| Option | Default | Meaning |
|---|---|---|
| `ring-seconds` / `ring-mib` | 8 s / 32 MiB | Memory-only pre-trigger history; smaller limit wins |
| `analysis-queue-seconds` / `analysis-queue-mib` | 5 s / 16 MiB | Actual-payload burst budget; overload remains counted |
| `queue-blocks` | 64 | Source queue; does not also define analysis capacity |
| `pre-seconds` / `post-seconds` | 0.5 / 0.5 s | Requested pre-trigger support and quiet tail, subject to availability/caps |
| `max-event-seconds` / `max-event-mib` | 8 s / 32 MiB | Per-event limits; continuous activity cannot extend indefinitely |
| `quota-mib` / `max-files` | 1024 / 256 | Managed capacity/count; oldest eligible events evicted |
| `keep-hours` | 0 | No age eviction by default; capacity/count limits still apply |
| `reserve-mib` | 256 | Required free-space reserve |
| Independent recording queue | 32 MiB | Bounds I/O stalls; not a growing disk spool |

At 2 MS/s CU8, data is about 4 MB/s; eight seconds is about 32 MB. Monitoring does not continuously write roughly 346 GB/day. High event volume can evict old data, so export important recordings outside managed storage.

## Running

```sh
signalscope monitor --output lora-events --frequency 470500000 --rate 1750000 --gain 38.6 --workers 4 --threshold-db 11 --seconds 0
signalscope monitor --input capture.sigmf-meta --output replay-events --seconds 0
```

The first command is a historical device-specific example; validate gain/rate on your device. `seconds=0` runs until stopped; `90000` sets a 25-hour budget without continuously saving all I/Q. File input is software replay and is labelled as such.

`monitor-status.json` is replaced at bounded intervals. It reports acquisition/analysis progress, rejected analysis samples/gaps, ring state, recording/eviction counts and at most 32 recent structure events. Normal automatic I/Q triggers accept `lora_structure_match`; CSS/probe candidates are diagnostic and do not become unbounded recordings. Explicit diagnostic modes have separate limits.

## Ownership and failure behavior

Only a new or valid owned event-store directory is accepted; locking rejects concurrent writers. Eviction touches only owned `event-*` directories with valid markers and rejects symlinks. Imported/manual/other-project data is not an eviction target.

Overlapping captures may merge within per-file caps. Metadata retains original configuration/global sample positions, interruptions, analysis gaps, missing pre-trigger history and truncation. Check `pretrigger_truncated`, `possibly_truncated` and `quiet_tail_observed`; file existence does not prove a complete packet.

The source-owned ring keeps raw recording continuity independent of analysis overload. Rejected analysis resets detector state; it must not join gaps into a fictitious frame. A continuous raw recording can outlive an analysis gap, but a missed detector event may never trigger saving. A late result can still arrive after relevant ring history was evicted.

The independent writer owns ring updates, file I/O, eviction and status writes. Acquisition offers bounded data to analysis and writer separately. `storage_status_stale=true` means the published snapshot is not fresh. Shutdown drains analysis then recording before final status. `recording_processed_pairs` is position consumed, not bytes saved; inspect event files for disk volume. Lag/high-water and dropped-result group/event counters remain separate. Recording queue exhaustion or file/metadata error stops explicitly rather than silently overwriting original data.

Probe diagnostics are throttled approximately once per candidate model per 0.25 s; this throttles reporting, not structure input. Unattended recovery still requires fault qualification.

## Soak observer and retained history

`python3 tools/soak_receiver.py --verify OBSERVATION_DIR` independently verifies a completed summary and indexed minute logs. It checks elapsed duration, complete log coverage, consistent process/install identity, sample progress, fresh status, losses, backlog and storage limits. Running/incomplete, corrupt or rotated-away evidence cannot return verified. Software observation does not prove USB physical continuity or calibrated sensitivity.

The read-only observer normally runs 86460 seconds with 60-second samples. It neither restarts receivers nor transmits nor records continuous I/Q. It checks Linux PID/start identity and install hashes. Atomic `summary.json` plus two rotating JSONL files (751 entries each, at most 1502 total) bounds storage. Observer failure does not stop acquisition; exit is nonzero. Repeated unchanged-but-not-yet-refreshed status is not confused with fresh frozen counters.

Historical 2026-11-04 start: Beijing 06:19:47, directory `/home/vicliu/SignalScope-validation/soak-1793744386750043983`, observer PID 947398, receiver PID 945720. This run **failed** after 1980.51 s with 2,424,832 rejected analysis pairs; subsequent count reached 10,551,296. A concurrent idle-priority build was stopped after preserving `failure-snapshot.json`; about 76 s of stable counters afterward supports a contention hypothesis, not causation or long-term success.

The bounded-buffer follow-up at 1.75 MS/s allowed about 4.79 s of analysis queue and eight seconds/about 26.7 MiB of history; acquisition remained 4 MiB and recording 32 MiB. UI lag is indicated over 0.2 s and highlighted over 0.5 s. Observer lag budget is declared queue time + one in-flight batch + 0.25 s (legacy fallback two seconds); recording lag remains capped at two seconds. Loss requirements were not relaxed.

Tests injected analysis pauses at progress callbacks and writer stalls through timed barriers. A local two-second pause exceeded the old 64-block high-water without loss, recording stayed byte-identical, and a six-second delayed result retained the head. Updated regression took 19.44 s. Field evidence included a five-minute GUI run without added analysis rejection; eight −9 dBm transmissions associated 5/8, retaining weak-edge failures. A roughly 2.5-second analysis pause reached 135 blocks, rejected no software input and recovered a second differently configured burst; both recordings covered expected complete I/Q intervals, not decoded payload/CRC.

A later historical start used `/home/vicliu/SignalScope-validation/soak-burst-1793755943851469655`, observer 1023135, receiver 1018833, requested 86460 s with an estimated 2026-11-05 09:33 Beijing finish. A start record is not a completed acceptance result. Another retained verify location is `~/SignalScope-validation/soak-sync-pair-20261105`; inspect its actual summary rather than assuming status from a pathname.

## Required coverage

No-signal/CSS-only no-I/Q behavior, valid LoRa save/replay, pre-trigger clipping, sustained triggers, count/size/age eviction, restart/lock conflicts, low disk, gaps, and 64-bit counters beyond 24 hours must be checked. Current wideband per-stage/resource rules in [specification 16](16-extension-boundaries.md) govern later implementations; short historical runs do not establish general lossless operation.
