# Third-party notices

English · [简体中文](THIRD_PARTY_NOTICES.zh-CN.md)

Foreshadow and its first-party plugins are GPL-3.0-or-later. The following
components retain their original licenses. This document describes the selected
dependency families; each binary package's `share/foreshadow/compliance/inventory.json`
is the versioned delivery inventory. `index.html` links its original notices,
source archives and build material. Open **Help → Licenses and source** in a
packaged desktop build. An independently downloaded receiver or research package
contains its own applicable materials.

| Component | Selected terms | Use and delivery |
|---|---|---|
| Qt 6 Core, Gui, Widgets, Network, Multimedia, SerialPort, Svg and deployed plugins | LGPL-3.0-only, with module-specific permissive third-party terms | Shared desktop libraries; exact module sources, SDK/source metadata, original notices and replacement instructions accompany the application. Qt translations retain BSD-3-Clause notices. The SDK source/notice set can contain tools and unused code; this is identified as a source superset, not a claim that those binaries ship. |
| Qt bundled third-party code | Original terms, including BSD, MIT, Apache, Zlib, Unicode and FreeType FTL alternatives | Preserve `qt_attribution.json`, applicable LICENSES and original copyright files in the source/notice set. Choose FreeType FTL where the alternative GPL-2.0-only option would conflict. No Qt Charts, Graphs, WebEngine or QML/Quick application feature is authorized merely by this list. |
| nlohmann/json 3.12.0 | MIT | Header code; copyright and full permission text retained. |
| KissFFT 131.1.0 | BSD-3-Clause | Static FFT fallback; include its actual COPYING copyright as well as the license text. |
| FFTW 3.3.10 | GPL-2.0-or-later, selecting GPLv3 for the combined delivery | Linux DSP and DTMB helpers. |
| VOLK 3.2.0 | LGPL-3.0-or-later | Linux vector processing; preserve exact Debian source and patches. |
| RTL-SDR 2.0.2 | GPL-2.0-or-later, selecting GPLv3 for the combined delivery | Receiver plugin; static on the reviewed Windows profile, system shared library on Debian. |
| libusb 1.0.x | LGPL-2.1-or-later | Receiver dependency; source and rebuild material for the static Windows link are included. |
| Pthreads4w 3.0.0 | Apache-2.0 | Windows RTL-SDR dependency; its original notices and source remain Apache licensed. |
| SoapySDR 0.8.1 | BSL-1.0 | Receiver abstraction; open-source SoapySDR does not grant rights in vendor APIs. |
| SoapyRemote 0.5.4, revision `bd10a2e4` | BSL-1.0 | Optional network receiver client; original pinned source and client build recipe accompany its separate package. No server or vendor API binary is bundled. |
| OpenSSL 3.x | Apache-2.0 | Catalog signature verification. The supported build minimum is now 3.0; an old 1.1.x library is not silently admitted under the 3.x declaration. |
| libarchive | BSD-style multi-file terms | Package extraction; preserve the complete upstream source notices. Windows compression dependencies include zlib (Zlib), bzip2 (bzip2 terms), XZ/liblzma (0BSD and original file notices), LZ4 (BSD-2-Clause) and Zstandard (BSD-3-Clause option). |
| FFmpeg `cd4e96eb314630788b7ff2f9820c395788c023e2` | LGPL-2.1-or-later for the configured library build | One native shared decoder set; `--disable-gpl --disable-nonfree`. The original source, `cmake/BuildFfmpeg.sh`, configure options and checked local portability patches accompany it. Runtime `-L`/`-buildconf` are additional evidence. Qt codec backends and their separate FFmpeg runtime are excluded from the reviewed PCM path. |
| dtmb-sdr `2cc94c012128d6fa3e8d6de456b3bb57e51990a9` | MIT | Source, original copyright, PN595/FEC patches and sampler notice retained. |
| DRA extracted fork `eb94ceb887b47b6ecfbda9da43273dd65246df1e` | MIT for the imported code | Vendored source and local changes are in `third_party/dra`; software copyright terms do not establish codec patent rights. |
| Natural Earth 1:50m land data | Public domain, recorded as `LicenseRef-Natural-Earth-Public-Domain` | The ADS-B module owns its [map notice](plugins/adsb/map-license.md), original/processed data hashes and conversion record. The imported contours keep their public-domain status; application code remains GPL-3.0-or-later. |
| Microsoft Visual C++ runtime | Microsoft runtime/Visual Studio redistribution terms | Unmodified Windows CRT files retain their vendor terms and are not relicensed GPL. Original runtime terms and the official redistribution list accompany Windows materials; the GPL System Libraries exclusion is recorded separately from the vendor's redistribution grant. |

## External and unresolved boundaries

**SDRplay:** The base application archive omits the optional receiver client.
The independent client contains the BSL-1.0 SoapyRemote 0.5.4 client at revision
`bd10a2e4a916bf66f0faa36dca7f043d4268138c`, together with original source, notices
and `cmake/RemoteReceiver.cmake`. It connects to an independent SoapySDRServer;
the client does not load local SoapySDRPlay3 or SDRplay API binaries. Install that
MIT driver and vendor API separately on the receiver computer under their terms.
The API 3.15 installer-v2/EULA investigation remains valid historical evidence,
not a newly acquired redistribution or linking grant. This external server boundary
does not carry the earlier direct-link condition into the base/client closure.
See [receiver services](docs/receiver-services.md) for the SDRconnect assessment,
service setup, protocol and qualification limits.

**Embedded pager firmware:** Arduino-ESP32, ESP-IDF, RadioLib, toolchains and SDK
binary libraries form a separate firmware boundary. Their terms and actual
link map must be checked before firmware publication. The desktop license does
not relabel a complete SDK or grant rights in radio firmware blobs. No firmware
image is distributed by the desktop package as part of this licensing change.

**Tools, fixtures and data:** Compiler/build tools and optional maintainer PDF
extraction tools are not desktop runtime dependencies. The optional existing
PyMuPDF table extractor has AGPL/commercial terms and is not executed, bundled
or made a prerequisite of a production source build. External standard PDFs,
test recordings, user captures and analysis output do not acquire a new license
from the root GPL file. Existing fixture provenance/dedications remain in place.

## Keeping notices accurate

The WSPR module vendors the WSJT-X Fano decoder and parity table. Original Phil
Karn/Joe Taylor notices, GPL and pinned source hashes remain under
`plugins/wspr/vendor/wsjtx`. Other upstream tools used for independent reference
generation are not linked into the application.
The bounded C++ source-field adapter acknowledges the WSJT-X field conventions
and original Joe Taylor/Steven Franke notices; it remains under GPL-3.0-or-later.

Providers register their legal roots at build/distribution assembly boundaries.
Reviewed platform profiles pin source hashes, selected license alternatives,
runtime ownership and build facts. The generic collector checks material
existence and hashes, package closure, actual executable ownership and unresolved
conditions. The signer reopens the archive and repeats these checks. Unknown
platform versions or new runtime files require new reviewed evidence; copying
an old inventory is insufficient. Mechanical evidence validation does not decide
disputed copyright ownership or replace missing permission.
