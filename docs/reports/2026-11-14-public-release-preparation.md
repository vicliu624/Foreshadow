# Repackaging and separate public-release preparation

English · [简体中文](2026-11-14-public-release-preparation.zh-CN.md)

Date: **14 November 2026**. Corrected delivery/source freeze:
`0c5fb5e42c61ccfa2605a248abc2b346fc92b7d3`. This supersedes the `f763a30`
archives for future publication. The later assessment/evidence commit does not
change these binaries. The [machine evidence](2026-11-14-public-release-preparation-proof.json)
records new archives, source-command execution, native package tests, physical
capture, actual codecs, replacement probes and separately prepared assets.

## Corrected and requalified

Removed the stray `+` from both languages' SoapyRemote offline CMake argument.
Repinned every reference to those build documents in the two reviewed platform
profiles, then generated new frozen source, legal materials and actual packages.
Normal composition and formal mechanical revalidation were used without a
private-review bypass. The corrected documents and nested source archive were
checked inside every applicable independent package.

The change axis is build/delivery documentation and its reviewed material data.
It changes no receiver protocol, provider, media implementation, UI, session or
installer behavior. Existing native base binaries match the prior qualified
runtime: **47 Windows / 20 Linux PE/ELF files**. Their libraries and decoder
configuration remain intact; no functionality was removed to simplify licensing.

| Check at this freeze | Windows x86-64 | Debian 13 x86-64 |
|---|---|---|
| English/Chinese command blocks identical | Passed | Passed |
| Clean source configure, build and both documented install commands | All four commands passed | All four commands passed |
| Application + RTL-SDR + SDRplay client + research package transactions | Passed | Passed |
| Actual archives reopened, independent base, Chinese/non-BMP paths | Passed | Passed |
| Extracted desktop/media/workflow checks | Passed | Passed |
| Formal mechanical archive/material gates | All four passed, no recorded issues | Same |
| Native license UI and compatible QtCore substitution/loading | Passed | Passed |
| New packaged physical service capture | 2 MS/s, 8 s, 16 million accepted I/Q pairs | Same |
| Actual FFmpeg decoder/configuration inspection | All configured decoders present, LGPL build flags retained | Same |

The licensing/package contract suite passed **17 tests**. Full native CTest
counts remain the dated `cb6fedf` baseline; no complete suite is falsely relabelled
as newly executed for this documentation change. Clean first-party/source-client
builds reuse exact existing Qt/native SDKs, not freshly rebuilt dependency SDKs.
Qt substitution uses compatible copied binaries with inert trailers. Physical
accepted counts do not establish long-term or hardware-counter losslessness.

## Prepared release units

| Unit | Readiness | Remaining action |
|---|---|---|
| Complete base | Actual packages and copyright materials qualified; television retained | Resolve the specific free-distribution codec patent decision, then release operations |
| SDRplay service client | Independently qualified and prepared unsigned | Publish compatible host availability/instructions, sign the official catalog and upload when publishing is authorized |
| Vendor API / pager firmware | Excluded, separate scope | No new redistribution/firmware clearance granted |

The [free/open-source codec assessment](../codec-patent-assessment.md) contains
primary evidence, jurisdiction/usage scope and a concrete unsent inquiry draft.
It does not turn a $0 royalty tier into a license exemption. No vendor messages,
patent contracts, fees or new noncommercial restrictions were introduced.
macOS and other Linux distribution/CPU baselines are outside this qualification.

Prepared directories:

- Windows: `C:\Users\vicliu\Apps\Foreshadow-0.2.12-Release-Preparation-20261114`.
- Linux original: `/home/vicliu/Applications/Foreshadow-0.2.12-Release-Preparation-20261114`.
- A hash-verified copy of the Linux asset set is under the Windows directory's
  `linux-debian13-x86_64/` subdirectory.

Each platform has `base/`, `sdrplay-client/`, `supporting-toolboxes/` and `sources/`.
The base has both the portable installation archive and the application update
package. Each group has its own `release-entries.json`; each platform has
`SHA256SUMS.txt` and `release-readiness.json`. The client requires host 0.2.12 or
later and an independently installed receiver service; its binary payload has no
television codecs or vendor API/driver. Source delivery remains a production-source
superset. The preparation directory also carries bilingual release notes and
assessment/report copies alongside, without changing verified archives.

| Asset | SHA-256 |
|---|---|
| Windows portable base | `700e596131d7de786925ec05e27748616583c17c63fa04d8ba87d752a49b060f` |
| Windows SDRplay toolbox | `7cbb26cd15ee8ddf269c4239783ecc75fda17ddea86b92a77b74f606232f5667` |
| Debian portable base | `adc20cfb1982962b2c09ceedacc8ff369b705c99de2026ff77473978668d109c` |
| Debian SDRplay toolbox | `74a1b0a6f8f692ccd32fa545bc017a60c02e65f3c683dc41b83f2587c56cf374` |
| Frozen first-party source | `ebae329693312951434e8af014ab3b7f18f402e297d45d4e5db752e2f0d40793` |

## Publication status

These are **prepared local release assets**, not a published release or patent
clearance. No production catalog signature, tag, public upload or hosted CI run
was created. Planned GitHub asset URLs are not represented as live downloads.
Read-only tag/release lookup found no `v0.2.12` tag/release through the successful
HTTPS Git lookup and repository release endpoint at the time of preparation;
the existing SSH remote lookup failed host-key verification and was not bypassed.

For publication, preserve per-package notices/source/replacement material and
make the source accessible with the downloads. Official catalog signing must
reopen these exact archives against their reviewed profiles, use the existing
trust anchor and an external production key, and revalidate any retained old
entries. Old direct-link archives are not silently grandfathered into the new
service release. The complete base's patent decision and the client's independent
readiness are recorded separately throughout this preparation.
