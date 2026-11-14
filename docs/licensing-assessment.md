# Licensing assessment and distribution gaps

English · [简体中文](licensing-assessment.zh-CN.md)

**Current update, 14 November 2026:** the in-process SDRplay arrangement assessed below has been replaced. The independent 0.2.12 base and network service client each pass their actual archive/material gates; neither includes or loads the vendor API/driver. See the [service guide](receiver-services.md) and [r4 qualification](reports/2026-11-14-receiver-service-review.md). The 13 November inventory and direct-link concerns below remain dated evidence; vendor API redistribution and firmware clearance remain separate.

The [free/open-source codec assessment](codec-patent-assessment.md) now evaluates the actual configured decoder set and primary licensing material. New packages at `0c5fb5e` correct the build example and pass requalification; [separate release preparation](reports/2026-11-14-public-release-preparation.md) keeps the full base's unresolved patent decision distinct from the client's readiness. No automatic free-download exemption or payable license is inferred.

Assessment date: 2026-11-13. This is an engineering assessment, not a license
grant or a completed legal clearance. The user's priority is satisfying upstream
requirements and avoiding incompatible distribution. The copyright holder has now authorized GPL-3.0-or-later for the main application and first-party plugins; the grant is in [LICENSING](../LICENSING.md). The PCM change is separate from that grant.

## Adopted licensing direction

For the existing full-featured open-source desktop application, **GPL-3.0-or-later
for first-party application code** is the adopted direction. Actual
Linux binaries link GPL-2.0-or-later FFTW; the RTL-SDR module links
GPL-2.0-or-later librtlsdr. Selecting GPL version 3 also accommodates current
Apache-2.0 dependencies and LGPLv3 Qt libraries. The user has authorized that first-party grant. It does not relicense
third-party code, clear the vendor API chain, or complete release obligations.

MIT/Apache-2.0 can license independently owned first-party files, but a permissive
root file cannot make a combined FFTW/librtlsdr binary permissively licensed.
Changing `dlopen` usage or packaging the module separately does not by itself
resolve that combined-work question. The FSF describes both static/dynamic
linking and communicating plugins in its [GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#GPLPlugins);
this is the licensor's interpretation, not a universal adjudication of every
plugin architecture. Apache confirms [compatibility with GPLv3](https://www.apache.org/foundation/license-faq.html#GPL).

**GPLv3 alone is not clearance for SDRplay.** The tested Linux chain is the
host → native receiver module → SoapySDR → SoapySDRPlay3 → `libsdrplay_api.so.3.15`
in the same process. SoapySDR and SoapySDRPlay3 licenses do not grant rights in
the vendor API. Obtain the exact API terms and resolve combined-distribution
permission before clearing that configuration. Installing an API separately
does not automatically settle linking compatibility. An exception granted only
by us cannot waive FFTW's or librtlsdr's conditions. A truly separate receiver
service with an appropriate protocol, or removal/replacement of relevant GPL
dependencies, is a possible engineering route requiring its own review.

## Evidence and scope

Inspected CMake link/installation rules, release/toolbox composition, pinned
sources, vendored notices, Windows vcpkg records/link commands, Qt 6.8.3 SPDX
SBOMs, the installed Windows review tree, and `ldd`/Debian copyright records on
the receiver-connected Debian 13 workstation. The baseline package was built
from `d7cfd271c1ee19e022051676fac48c545dab7381`; the initial PCM change is commit
`7ba437a`; native-device conversion and the output-test discovery correction are
`e44c8a7dd9a6ae41a5058b1640b4326dc3ba9650`. The [inventory evidence](reports/2026-11-13-license-inventory.json) preserves observed SDK metadata and binary license responses. The final PCM/runtime source is `a258670d92525c43e5dd6cf104b9965108c9198c`; see the [qualification and local deployments](reports/2026-11-13-pcm-and-licensing-review.md).

This inventory establishes direct dependencies and important transitive groups.
It is **not a complete per-file, per-release SBOM**. Qt module SBOMs contain
tools and unused modules as well as runtime components; copying their entire
list would incorrectly claim that everything ships. The exact deployed closure,
license choice and source delivery must be generated for each final artifact.
Native macOS closure/installation was not requalified in this assessment.

## Application and receiver dependencies

| Component | Observed source/version and use | Upstream license | Distribution action |
|---|---|---|---|
| nlohmann/json | 3.12.0, hash-pinned; compiled header code | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) | Preserve copyright and permission notice in source and binary materials. |
| KissFFT | 131.1.0, hash-pinned; static FFT fallback | [BSD-3-Clause](https://github.com/mborgerding/kissfft/blob/131.1.0/COPYING) | Include the actual copyright notice and BSD terms, not only the generic license template. |
| FFTW | Linux 3.3.10; linked by the host DSP and DTMB helpers; absent from the inspected Windows build | [GPL-2.0-or-later](https://fftw.org/doc/License-and-Copyright.html) | GPL-compatible combined distribution and corresponding source/build material; commercial alternatives require an actual grant. Dynamic linking is not an exemption. |
| VOLK | Linux 3.2.0; optional vector DSP | [LGPL-3.0-or-later](https://github.com/gnuradio/volk/blob/v3.2.0/kernels/volk/volk_32fc_magnitude_32f.h) | Preserve terms/notices, corresponding library source and applicable replacement rights. Older 2.x GPL licensing must not be substituted for the inspected 3.2.0 license. |
| librtlsdr | Windows/Linux 2.0.2; native RTL receiver module; Windows static link | [GPL-2.0-or-later](https://github.com/osmocom/rtl-sdr/blob/v2.0.2/src/librtlsdr.c) | Supply source and build/patch information for the module's complete linked work. A separate toolbox still has obligations. |
| libusb | Windows 1.0.30 static in RTL module; Linux 1.0.28 system library | [LGPL-2.1-or-later](https://github.com/libusb/libusb/blob/v1.0.30/libusb/core.c) | Select and document a permitted compliance route. Static inclusion needs a relinkable/corresponding-source solution, not just COPYING. |
| Pthreads4w | Windows 3.0.0, static `pthreadVC3.lib` in RTL module | Apache-2.0 in the inspected vcpkg copyright file | Preserve license and applicable notices; retain exact source/patch provenance. |
| SoapySDR | 0.8.1; Windows shared DLL, Linux system library | [BSL-1.0](https://github.com/pothosware/SoapySDR/blob/soapy-sdr-0.8.1/LICENSE_1_0.txt) | Keep provenance and notices with source/materials; assess each driver separately. |
| SoapySDRPlay3 | Workstation-installed native driver | [MIT](https://github.com/pothosware/SoapySDRPlay3/blob/master/SoapySDRPlay.hpp) | Its precise installed revision is not pinned by this repository. Record it before redistribution; MIT does not cover the API below it. |
| SDRplay API | Workstation resolves `libsdrplay_api.so.3.15`; outside the inspected application archive | [Vendor API terms](https://sdrplay.com/hardware-api/), exact grant unresolved | Obtain/preserve version-specific EULA and redistribution/linking permissions. No open-source or redistribution grant is inferred from a free download. |
| OpenSSL | Windows 3.6.5 static crypto; Linux 3.5.7 system library | [Apache-2.0 for 3.x](https://openssl-library.org/source/license/index.html) | Include terms/notices and exact source identity. CMake now requires 3.0+; reviewed profiles bind the actual version and do not apply the 3.x conclusion to older 1.1.1 terms. |
| libarchive | Windows 3.8.9 static; Linux 3.7.4 system library | [BSD-style multi-file terms](https://github.com/libarchive/libarchive/blob/v3.8.9/COPYING), primarily BSD-2-Clause | Preserve the upstream COPYING and per-file terms; also inventory the linked compression/crypto dependencies. |

The Windows libarchive dependency records include zlib 1.3.2 (Zlib), bzip2 1.0.8
(bzip2 terms), liblzma 5.8.4 (library: 0BSD), LZ4 1.11.1 (library: BSD-2-Clause),
and Zstandard 1.5.7 (library: BSD-3-Clause or GPL-2.0-only; choose the BSD option).
Their exact copyright files are present in the build cache but absent from the
inspected installed notice tree. libarchive also includes per-file public-domain
and permissive notices: a single “BSD” line is insufficient. Debian's system
closure includes additional libraries such as libxml2, nettle and ACL. They were
not copied into the application archive; bundling them in a future portable
release would require a fresh runtime and source inventory.

## Qt: permitted use and the proof we need

Windows uses Qt **6.8.3** and the workstation uses Debian Qt **6.8.2**. The
inspected Windows SBOMs offer the runtime modules below under LGPL-3.0-only,
with other GPL/commercial alternatives. We can use the LGPL route when its
conditions are met; commercial use alone does not make a commercial Qt purchase
mandatory. See [Qt 6.8 licensing](https://doc.qt.io/qt-6.8/licensing.html) and
[Qt's LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).

| Qt component | Actual role | Scope decision |
|---|---|---|
| Core / Gui / Widgets | Desktop, resources, widgets, rendering | Retain shared runtimes; Qt is not directly imported by `src/backend` or its public backend headers. |
| Network | Desktop network transport adapter | Retain within the platform/network adapter boundary. |
| Multimedia | Native PCM device output | Retain QAudioSink/QMediaDevices; research WAV playback no longer needs QMediaPlayer. |
| SerialPort | Pager serial utility | Retain for this executable; include its license in the executable's closure. |
| Svg and platform/image/icon/TLS/style plugins | Present in automatic Windows deployment | Attribute what actually ships; remove an item only after dependency and behavior checks. |
| Qt translations | Qt widget localization; current 6.8.3 SPDX entry offers BSD-3-Clause | Retain exact notices; English UI does not eliminate the shipped language resources. |
| LinguistTools, moc, rcc, deployment tools | Build tools | Distinguish tool licenses/Qt exceptions from linked runtime licensing. Their presence in a build does not mean the generated application is a GPL-only Qt module. |
| WebEngine, Charts, Quick/QML, Graphs, Virtual Keyboard | Not present in the inspected Windows runtime | No new dependency is authorized merely by being available in the SDK. Review the exact module and release license before adding it. |

Required Qt release evidence:

1. Fix the Qt patch release, binary source/provenance, module/plugin closure and
   selected open-source license. A rolling `brew install qt` or unpinned package
   update is not a permanent license record.
2. Ship the applicable LGPL/GPL text, prominent Qt attribution and component
   copyrights. Preserve internal third-party notices and applicable source
   requirements, rather than only linking to a Qt website.
3. Make the exact Qt corresponding source and local changes/build information
   available through a valid license route. Prefer immutable source assets next
   to the binary release over an undocumented “ask upstream” arrangement.
4. Keep the shared libraries replaceable and provide build/replacement guidance.
   Where installation information is required, include it. macOS signing and
   user re-signing require native verification; do not claim success based only
   on a DLL being shared.
5. Avoid EULA clauses that prohibit the modification/reverse engineering needed
   to exercise LGPL rights. Official catalog trust must not become a blanket
   prohibition on a recipient exercising rights in local copies.
6. Reject new modules with unknown or incompatible terms during release checks.
   Buying Qt commercially would not waive FFmpeg or other independent licenses.

Qt's own third-party components are a separate layer. The inspected SDK SBOMs
include PCRE2/sljit, zlib, libpng, libjpeg-turbo, FreeType, HarfBuzz, Unicode/CLDR,
double-conversion, TinyCBOR, md4c, public-suffix data and additional small
attributions. Relevant terms include BSD/MIT, Zlib, libpng/IJG, FTL, Unicode-3.0,
MPL-2.0 and specific license references. In particular, select FreeType's **FTL**
route for the GPLv3-compatible bundle rather than assuming its alternative
GPL-2.0-only choice is interchangeable. Filter the SDK SBOM to the actual
shipped closure and copy applicable original notices/source materials. Do not
turn every SDK package or NOASSERTION entry into a verified runtime license.

The inspected review package contains Qt DLLs but lacks this complete Qt notice
and source-delivery set. **Its functional package acceptance is not a license
compliance acceptance.** No promise that upstream can never send a notice is
possible; documented fulfillment of the actual conditions is the practical goal.

## Television codecs and the duplicate FFmpeg correction

| Component | Fixed source | License / action |
|---|---|---|
| dtmb-sdr | `2cc94c012128d6fa3e8d6de456b3bb57e51990a9` | [MIT](https://github.com/ningzichun/dtmb-sdr/blob/2cc94c012128d6fa3e8d6de456b3bb57e51990a9/LICENSE); retain notice, sampler attribution and local patch records. Helpers linked with FFTW additionally need a GPL-compatible distribution. |
| Native FFmpeg | `cd4e96eb314630788b7ff2f9820c395788c023e2` | [LGPL-2.1-or-later for the selected build](https://ffmpeg.org/legal.html); shared, GPL/nonfree disabled. Exact full source archive and build recipe already accompany the television notices. Review prominent attribution and replacement guidance as well. |
| DRA core | Extracted from `eb94ceb887b47b6ecfbda9da43273dd65246df1e` | Upstream repository [MIT notice](https://github.com/maliwen2015/ffmpeg_cavs_dra/blob/eb94ceb887b47b6ecfbda9da43273dd65246df1e/LICENSE); extracted files, hashes and local fixes are recorded in [UPSTREAM](../third_party/dra/UPSTREAM.md). Copyright notices do not establish patent clearance. |

The old Windows review bundle contained `avcodec-61.dll` from Qt and
`avcodec-63.dll` from the native television SDK. Calling each DLL's own
`avcodec_license()` reported LGPL version 2.1 or later, but those were still two
independent source/build identities. Only the native SDK had a source archive
in the inspected package. The second runtime was not merely an accidental
unused file: the generic analysis panel used QMediaPlayer for provider WAVs.

The user authorized replacing that playback path with validated finite PCM
output. `ResearchAudio` remains canonical 16-bit PCM WAV, mono/stereo, 8–192 kHz;
the owning research module validates the result and handles QAudioSink lifecycle.
When necessary, a finite source converts rate, channel count and sample type to
the device preferred format in chunks of at most 64 KiB. It never materializes
a full resampled output; the original WAV is unchanged. Conversion is an audio
presentation operation, not a change to recovered samples or evidence.
The host still consumes a generic audio result and exports the original WAV.
FM and television already use PCM output. No provider-ID branch or new sample
codec assumption is introduced.

A process-mapping probe found that merely replacing QMediaPlayer was insufficient on Debian: Qt still selected its installed FFmpeg plugin during audio-device initialization. The owning audio module now explicitly initializes the built-in PCM-capable fallback and restores the environment immediately, so separately launched research tools keep their own backend settings. This follows the inspected [Qt 6.8.2 implementation](https://github.com/qt/qtmultimedia/blob/v6.8.2/src/multimedia/platform/qplatformmediaintegration.cpp) and [Qt 6.8.3 implementation](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/platform/qplatformmediaintegration.cpp). Loaded-image tests reject a codec backend in the PCM process; upgrades must pass this behavior contract. This is verified behavior for the stated releases, not a claim about every future Qt release.

On Qt 6.8+ Windows deployment, skip multimedia codec plugins and Qt's FFmpeg
libraries. The macOS install assembly removes those codec plugins/foreign FFmpeg
ABIs and restores the owning native SDK before Mach-O relocation; its independent
fixture verifies preservation and repeatability, not native Mac acceptance.
Linux archives already use system Qt and carry only the native SDK; inspect the
installed system separately. Lower Qt versions are not covered by the new
deployment-filter guarantee. Actual archive checks require one real runtime per
FFmpeg library and run PCM validation/output qualification against the extracted
runtime. Output-device absence is a declared skip, not successful playback.

Codec copyright licenses and codec patent rights are different questions. This
assessment does not clear AVS/DRA/H.264/H.265/AAC patents for every jurisdiction
or commercial distribution. External recorded reference media are validation
inputs and remain outside source/releases.

## Pager firmware and historical source copies

The repository also contains `firmware/pager_tx`; this is a separate embedded
artifact, not a desktop runtime dependency. Its PlatformIO configuration pins
`espressif32@6.10.0` and RadioLib 7.4.0, with the Arduino framework.

| Component | Declared license / evidence | Required treatment |
|---|---|---|
| RadioLib 7.4.0 | [MIT in the pinned upstream manifest](https://github.com/jgromes/RadioLib/blob/7.4.0/library.json) | Preserve its actual copyright/license file with a distributed firmware source/binary set. |
| PlatformIO espressif32 6.10.0 | [Apache-2.0 platform manifest](https://github.com/platformio/platform-espressif32/blob/v6.10.0/platform.json); Arduino package range `~3.20017.0` | Record the resolved package/toolchain versions and hashes; a platform pin is not a complete firmware bill of materials. |
| Arduino-ESP32 core | [LGPL-2.1](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md); the available `3.20017.241212+sha.dcc1105b` cache declares LGPL-2.1-or-later | Static firmware distribution must provide the applicable library source/changes and a valid relinking route, including sufficient application objects or reproducible source/build material. |
| ESP-IDF/embedded SDK and runtimes | [Apache-2.0 original code plus per-component terms](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/COPYRIGHT.html), including MIT/BSD components and separately licensed items | Derive actual components from the final ELF/link map and exact framework package. Do not label every SDK archive Apache-2.0 or assume that an unused radio feature's binary is linked/absent without evidence. |

The original baseline had not resolved an actual firmware build. The later build and allocated-section evidence are now recorded below; firmware license clearance remains open. Scope the
initial desktop GPL recommendation explicitly; give firmware its own reviewed
scope. Permissive terms for independently owned firmware code can be considered,
but cannot remove Arduino's LGPL or SDK conditions.

`research/v4/source-before-buffering.tar` contains 84 historical source entries
and no license-named member; `patches/v3-periodic-fsk` also preserves older source
copies. Preserve their evidence identity. The eventual licensing scope/inventory
must account for these first-party copies and any imported material; a new root
file is not proof that every nested historical artifact has a complete notice set.

## Tools, test data and documentation

Python is a maintenance/testing tool, not a required application runtime. Optional
NumPy/SciPy (BSD), Matplotlib (its PSF-based license), pySerial (BSD-3-Clause), and
esptool (GPL-2.0-or-later) must be recorded if we distribute those tools/packages.
Their presence on a developer machine does not automatically relicense DSP code.
The repository's `extract_tables.py` imports **PyMuPDF**, which offers
[AGPL/commercial licensing](https://pymupdf.io/licensing). Review the extractor
as its own linked tool; replace that dependency or explicitly satisfy its terms.
Do not infer that the native application is AGPL, or ignore the extractor because
it is not in the desktop archive.

CMake, Ninja, NASM, Make, compilers and MSYS2 are build tools. The old Windows baseline also contained MSVC runtimes, D3DCompiler and software OpenGL. The new Widgets/PCM deployment omits the unused D3D compiler and software OpenGL; required MSVC runtimes remain. Preserve the actual
Microsoft redistributable authorization/provenance and the software renderer's
third-party notices. Linux system C/C++ runtimes and macOS LLVM runtimes have
their own licenses/exceptions; any copied runtime needs inventory. The presence
of GPL build tools alone is not proof of GPL runtime linkage.

Generated television fixture media/provenance already use CC0-1.0. Preserve that
scope; it is not an application license. Record source/rights for RF fixtures,
public interoperability vectors, standard-derived numerical tables and old
report images. Public accessibility does not supply a redistribution grant for
broadcast recordings or standard PDFs. User recordings and exported analysis
data do not become GPL merely because the application generated/opened them.
First-party docs/screenshots can use a separately stated CC-BY-4.0 grant if their
owners choose it; imported notices/data keep their own terms.

## Implemented adoption and release protocol

The root GPL text is unmodified; [LICENSING](../LICENSING.md) states the first-party scope and exceptions. [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md) and [SOURCE_AND_REPLACEMENT](../SOURCE_AND_REPLACEMENT.md) are bilingual delivery documents. The original inventory above describes the previous package baseline; it must not be substituted for a new package's generated inventory.

Provider-owned schema-1 declarations register package roots. Reviewed platform profiles pin exact source artifacts and their SHA-256, selected license alternatives, runtime ownership and Qt/OpenSSL/application build facts. The generic collector assembles real source archives, original notices and build/patch recipes. Every independent receiver/research archive gets its own closure. The desktop Help menu opens the generated material index. Stable receiver/session/UI/installer consumers contain no provider-ID licensing branches.

Packaging rejects missing or modified materials, unknown runtime binaries and build/source mismatches. An explicit private-review mode preserves visible unresolved conditions; it does not bypass those checks. The formal signer reopens archives, verifies their materials and compares identities, closure, conditions and runtime rules with a repository-reviewed profile. Erasing an issue from a generated inventory cannot authorize publication. Formal release workflow inputs pin the reviewed material archive by HTTPS URL and SHA-256; an unreviewed platform/version has no fallback clearance.

SDRplay evidence improved after the original baseline: the workstation cache contains the API 3.15 installer v2. Its AMD64 API library SHA-256 matches the installed `/usr/local/lib/libsdrplay_api.so.3.15`; its included EULA is effective 2021-01-12 and has the same hash as the older public mirror. Thus these are version-bound installed terms, not merely a guessed mirror. They retain hardware-use and reverse-engineering restrictions and do not establish redistribution/GPL combined-work permission. No vendor binary is bundled; the relevant profile remains review-only.

The cached SoapySDRPlay3 source is commit `48bd8b41072534018de1d74deb3dea5874d9e0e0`. The installed driver and cached build share GNU build ID `7f707482ac60e32a070d4dae61a838de2ed996a7`; installed and uninstalled ELF byte hashes differ, so the evidence records that distinction. Its MIT source/notice is delivered as supporting material, not permission in the vendor API.

A real `pager_tx` build now resolves platform 6.10.0, Arduino package `3.20017.241212+sha.dcc1105b` and RadioLib 7.4.0. The initial archive-inclusion list names radio libraries, but linker garbage collection discards their unused sections. A second check of positive-size allocated input sections finds live contributions from `libcoexist.a` (1,275 bytes), open SDK wrappers and other runtime archives; it finds no live contribution from `libnet80211.a`, `libpp.a`, `libphy.a`, `libbtbb.a` or `libbtdm_app.a`. Do not confuse the initial archive list with final ELF contents. The current upstream coexistence-library repository declares Apache-2.0 for its object form, but the exact SDK revision/source obligations still need separate firmware qualification. The [firmware evidence](reports/2026-11-13-firmware-license-evidence.json) records versions, archive names and ELF/map hashes. Nothing was flashed or transmitted. Firmware source/relink material, per-component vendor terms and its separate grant remain open before firmware publication.

The public-release conclusion remains **not cleared** for the SDRplay configuration and firmware. Source/notice delivery and native functional acceptance are recorded per new review artifact; neither is a universal legal assurance. macOS is outside this Windows/Linux qualification.

## 2026-11-14 service boundary supersedes the direct-link profile

The 2026-11-13 direct-link package conclusions above remain dated evidence. In
0.2.12 the base omits the optional SDRplay client, and the client uses the BSL
SoapyRemote network protocol to an independently installed service. Vendor API
and local-driver binaries are neither bundled nor loaded by that client. The
base/client profiles therefore evaluate their actual closures without the former
direct-link permission issue. The API EULA and firmware are not relicensed or
newly cleared. The [service assessment](receiver-services.md) records SDRconnect
control gaps, external native-trial blockage, fallback choice and limits.
