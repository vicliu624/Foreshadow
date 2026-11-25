# Resource lifetime audit — 2026-11-25

English · [简体中文](2026-11-25-resource-lifetime.zh-CN.md)

## Scope and incident evidence

The operator reported `Too many open files` on Debian x64 host `.42` after
overnight use, then restarted the computer. The original process and its
descriptor table are unavailable. This audit therefore distinguishes reproduced
implementation defects from attribution of that particular incident.

The working changes are based on `9bfeb363b5489018e0c6e293ddc5d9e826d387da`.
Tests use an isolated source/build tree under the existing local runner account
on `.42`. They do not replace the installed 0.2.15 release, alter its signed
catalog, or open the physical RSPdx-R2. No full Windows/macOS release-archive acceptance or
overnight hardware soak is claimed by this report.

A subsequent native Windows MSVC client preflight reproduced failure with the
original unpatched dependency and passed with the reviewed patches. After a
fully closed WinSock warmup and confirmed real log delivery, both 32 construction
cycles and 16 recovery cycles ended at their baseline of 150 handles. Counts
require exact equality. The formal Windows test link also uses the existing
receiver SDK interface target to preserve imported dependency scope. This is
source-level client verification, separate from full release-archive acceptance;
macOS and overnight physical operation remain unverified.

The Windows recording preflight also reproduced a test-harness failure: a first
process notification can carry only startup diagnostics, and Windows text output
uses CRLF. The corrected test waits for a complete stdout readiness line until a
deadline, normalizes its line ending and retains diagnostics on failure. It
directly checks the native CRT lock handle's inheritance flag, because Qt can
restrict child inheritance independently. The handle is matched by volume/file
identity so Windows short paths and junctions do not change the assertion.
Removing `_O_NOINHERIT` made that
native assertion fail; restoring it passed all 64 lock/recording/replay cycles
with 148 handles before and after. This is separate from the original production
defects and does not waive the frozen-source formal release gates.

## Confirmed defects and corrections

| Defect | Before correction on native Linux | Correction |
|---|---|---|
| Remote logging recovery replaces an owned socket and thread pointer without reclaiming the previous resources | One injected logging disconnect/recovery raises the native descriptor count from 6 to 7 | Join the completed logging thread, clear its pointer and close the previous socket before reconnecting; do not reconnect an unsubscribed handler; use an atomic completion flag |
| Remote construction fails after acquiring a raw log subscription | A rejected remote MAKE raises the descriptor count from 4 to 5 after unwinding | Keep the subscription in a local `unique_ptr` until the complete constructor succeeds |
| Event-store lock descriptor is inheritable | The native `FD_CLOEXEC` check fails | Open the POSIX lock with `O_CLOEXEC`, and the Windows CRT lock with `_O_NOINHERIT` |

The reconnect defect can accumulate resources when this recovery path repeats.
The failed constructor retains a shared log subscription/reference; it is not
accurate to claim that every repeated failure against the same server necessarily
adds another descriptor. The lock defect can prolong ownership through a child
process; it does not itself demonstrate monotonically increasing descriptors in
the parent. None of these observations identifies the unique cause of the
already restarted overnight process.

## Ownership and architecture

| Change axis | Owner | Stable consumers | Assembly/change location |
|---|---|---|---|
| Recording-directory exclusivity and lock lifetime | Recording backend | Monitor/session callers remain unchanged | `src/backend/recordings/event_store.cpp` |
| Remote receiver connection/log lifetime | Optional receiver client's pinned dependency | Receiver session, CLI, GUI and generic installer remain unchanged | `cmake/RemoteReceiver.cmake` and `third_party/soapyremote-patches/` |
| Guarded dependency source preparation | Build assembly | Existing DTMB patch callers retain their entry point | Shared `cmake/PatchPinnedSource.cmake`, with the DTMB compatibility wrapper |

The changes add no receiver-ID dispatch to stable consumers. The upstream source
download is unchanged. Reviewed changes are generated in private build-tree
copies; each expected context must match exactly once. The original independent
upstream receiver service is not patched or bundled with the client.

The [client patch notes](../../third_party/soapyremote-patches/README.md) document
the original BSL-1.0 notices and local changes under the same license. The three
reviewed Windows/Debian x64/ARM64 material profiles include the patch manifests,
build helper and bilingual notes. Their repository-file hashes and the bilingual
source/replacement instructions are updated together.

## Acceptance and limits

The same native tests were run with the three ownership fixes removed from an
isolated copy. Both new test cases failed, including the 4→5 and 6→7 descriptor
changes above. Restoring the fixes made them pass.

| Check | Same-process coverage | Patched native descriptor result |
|---|---|---|
| `event_store_resource_lifetime` | 64 store open/conflicting-open/release cycles; a child stays alive while the parent releases and reacquires the lock; recorder completion and replay success, consumer failure and cancellation across CU8/CI16 fixtures | Baseline and final count 5 |
| `remote_resource_lifetime` | 32 rejected MAKE requests interleaved with successful construction/release; 16 deliberate logging-channel disconnects through a TCP relay; final unsubscribe | Baseline and final count 4; no increase across reconnects |
| `sdrplay_backend_contract` | 32 additional full provider stream cycles, covering success, cancellation, consumer failure and activation failure | Baseline and final count 4 |

These are native resource-count checks against a real, independent upstream
service and an independent test receiver. Running a fresh CLI for every operation
would let process exit hide resource retention; the checks above intentionally
reuse the client process. The TCP relay breaks only the logging channel while
keeping the command connection alive.

An isolated offscreen idle run of the installed 0.2.15 GUI on `.42` observed 120
one-second samples: the descriptor count stayed at 11, including startup. This
does not exercise the operator's original graphics session, continuous physical
capture, prolonged media playback or overnight reconnect conditions.

Regular metadata/replay streams, recording completion, receiver device/stream
guards, subprocess shutdown and distribution transport/store ownership were also
reviewed. This is not a guarantee that every third-party path is leak-free.

The complete native CTest run registered 132 cases: 130 passed, two were skipped
because the isolated runner account has no audio output device, and none failed.
The skipped cases were `research_audio_output` and `fm_playback_recovery`.
The suite included the 140-second WSPR and 60-second ADS-B sustained-input checks.
After the final source/material sync, the three lifetime/backend checks,
extension architecture contract and licensing contract passed again. The latter
also passed all 27 Python cases on Windows, and documentation validation passed
for 93 bilingual pairs. This is not Windows native application acceptance.

An actual install tree in a path containing Chinese and a non-BMP emoji passed
the existing packaged receiver and desktop checks, including module discovery,
CLI recording/replay and desktop replay/analysis/data protection. The first
installation attempt used a development SDK override without the original
FFmpeg download in this private cache. The original archive was then supplied
after verifying SHA-256
`e3976ef0636e9c12d68c193abc4898ab50f05be3a4996026abc60c18be7447d4`,
and the complete checks passed. No source/notice check was bypassed. This is an
installed-tree qualification, not a newly published formal release archive.
