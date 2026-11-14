# Corresponding source and library replacement

English · [简体中文](SOURCE_AND_REPLACEMENT.zh-CN.md)

Every qualified package carries `share/foreshadow/compliance/`. Start with its
`index.html`, `inventory.json` and `verification.json`. Source archives are real
files beside their original notices; a homepage link is not substituted for
source delivery. The source commit, platform versions, material SHA-256 values,
runtime paths and unresolved conditions identify this particular delivery.
The project adds no restriction on modification or reverse engineering needed
to debug modifications of LGPL libraries.

## Source contents

The `foreshadow/source/` archive contains the frozen production source, all
first-party plugins, SDK headers, resources, CMake installation rules and
maintenance/build scripts. `SOURCE-DELIVERY.json` binds it to the Git commit and
lists omitted nonproduction research archives, old patches, external test
fixtures and the separately reviewed embedded firmware. Those assets are not needed to compile the application with
`BUILD_TESTING=OFF`. The full repository retains their history and provenance;
they are not deleted by creating a delivery archive.

Dependency directories contain pinned original upstream archives, applicable
distribution patches/build recipes and original notices. Windows vcpkg recipes
include the actual port definitions, patches, triplets, scripts, baseline and
installed dependency metadata. Qt material includes module source archives,
upstream build information and SDK SPDX metadata. Debian material includes the
exact source package `.dsc`, original tarball and Debian patch tarball. Retain
all files of a Debian source package when unpacking it with `dpkg-source -x`.
The HTTPS origin and SHA-256 checks are recorded; a downloaded `.dsc` is not
described as independently signature-verified unless that was actually done.

## Rebuild the application

Use CMake 3.20+, a C++20 compiler and the dependencies listed in
[the build guide](docs/07-building-and-cli.md). The Windows review profile uses
MSVC 2022, Qt 6.8.3 and the pinned vcpkg source/port set; the Linux review profile
uses Debian 13, GCC and Qt 6.8.2 source packages. OpenSSL must be 3.0 or later.
Do not substitute a different dependency release while claiming to have rebuilt
the exact delivered version.

1. Extract the Foreshadow source archive and each required upstream source.
   On Debian, extract the corresponding source package and apply its Debian
   patch series with `dpkg-source -x package.dsc` (install `dpkg-dev` if needed).
   On Windows, use the delivered vcpkg port/patch/triplet metadata to reproduce
   the dependency builds. A compiler or OS SDK is a build prerequisite, not
   included as a relicensed Foreshadow component.
2. Build/install Qt and native dependencies from their delivered source and
   recipes, or use ABI-compatible development packages for a modified build.
   Keep the platform plugin and module set consistent with the Qt build.
3. Configure the application, explicitly pointing FetchContent at extracted
   sources. Paths below are placeholders for your local extraction directories:

```sh
cmake -S foreshadow -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSIGNALSCOPE_REQUIRE_GUI=ON -DSIGNALSCOPE_REQUIRE_AUDIO=ON \
  -DFETCHCONTENT_SOURCE_DIR_JSON=/sources/json \
  -DFETCHCONTENT_SOURCE_DIR_KISSFFT=/sources/kissfft \
  -DFETCHCONTENT_SOURCE_DIR_DTMB_DECODER=/sources/dtmb-sdr \
  -DFETCHCONTENT_SOURCE_DIR_FFMPEG_NATIVE=/sources/ffmpeg \
  -DFETCHCONTENT_SOURCE_DIR_SOAPYREMOTE=/sources/SoapyRemote
cmake --build build --config Release --parallel 4
cmake --install build --config Release --component SignalScope --prefix /output
cmake --install build --config Release --component ResearchExamples --prefix /output
```

On Windows run from the x64 Native Tools command prompt, use Windows paths and
`-DCMAKE_TOOLCHAIN_FILE=.../scripts/buildsystems/vcpkg.cmake`, the delivered
triplet and `-DSoapySDR_DIR=.../share/soapysdr`. The native FFmpeg build also
needs Bash, Make and NASM; see `cmake/MediaDecoder.cmake` and
`cmake/BuildFfmpeg.sh` for the exact configured decoder set and local portability
patches. `cmake/DtmbReceiver.cmake` applies the delivered declarative DTMB patches.
Production builds do not need PyMuPDF, the external standard PDF or RF captures.
Reproducibility here means source/build correspondence and a functional rebuild;
it does not promise byte-identical compiler timestamps or signatures.

## Replace Qt or FFmpeg

Work on a copy of the portable installation with the application stopped. Keep
the original copy for rollback. Foreshadow does not cryptographically lock the
Qt/FFmpeg DLLs or reject a user-modified compatible library at startup. Signed
online catalogs authenticate downloaded packages; they do not remove the ability
to build and run a local modified installation. Updating a managed package can
replace local modifications, so keep a separate modified copy.

- **Windows Qt:** replace the applicable `bin/Qt6*.dll` and matching plugin
  binaries under `plugins/` with a shared build using the same architecture,
  compiler runtime, Qt module configuration and a compatible ABI. Include matching
  dependencies. Do not mix debug Qt DLLs with a release application. The reviewed
  Widgets/PCM application omits Qt multimedia codec plugins and the unused
  software OpenGL deployment; keep its required Windows platform plugin.
- **Linux Qt:** Qt is supplied by the system. Install your compatible shared
  build in a separate prefix and run a copied application with that prefix's
  `lib` in `LD_LIBRARY_PATH` and its plugin directory in `QT_PLUGIN_PATH`, or
  rebuild Foreshadow against it. Avoid pointing plugin search paths at unrelated
  versions. Record the actual loaded paths with `/proc/<pid>/maps` or the loader's
  diagnostics when checking substitution.
- **FFmpeg:** replace the native decoder's shared `avcodec`, `avformat`,
  `avutil`, `swresample` and `swscale` libraries as a compatible set in `bin/`
  (Windows) or `lib/` (Linux). Rebuild the application/media helper if the public
  ABI changes. Run the media decode and playback checks on the modified copy.
  Modified FFmpeg with GPL/nonfree features requires a new licensing review;
  the supplied LGPL declaration only describes the recorded configuration.
- **Statically linked dependencies:** rebuild them from the included source
  and port patches, then relink the included first-party source. This is also
  the replacement route for the Windows RTL-SDR/libusb link; supplying only a
  replacement DLL would not replace code statically linked into the module.

These instructions are mechanisms and prerequisites, not a guarantee that every
arbitrary library modification remains ABI-compatible. Native macOS replacement,
signing and bundle acceptance are outside the current Windows/Linux qualification.

## Regenerate and check distribution material

`tools/distribution/make_source_bundle.py` creates frozen source;
`collect_materials.py` consumes the reviewed platform profile and a hash-checked
artifact cache. `compliance.py` attaches the material to a fresh CMake installation.
`package_release.py` resolves each provider's package closure and embeds it in
every independent archive. Use `--review-only` solely for clearly recorded private
evaluation conditions. `sign_catalog.py` reopens every archive and rejects missing
or changed materials, unowned runtimes and unresolved distribution conditions.
The generic installer does not interpret license IDs or provider identities.
When extending an existing signed catalog, supply the archives and entry files
for retained versions too: the signer revalidates retained entries rather than
silently carrying forward packages built before these checks. Keep their reviewed
platform profiles in the repository for as long as those versions are offered.

```sh
python tools/distribution/make_source_bundle.py --commit COMMIT --output source.tar.gz
python tools/distribution/collect_materials.py --profile PROFILE.json --cache CACHE \
  --first-party-source source.tar.gz --source-commit COMMIT --output MATERIALS
python tools/distribution/compliance.py --stage STAGE --materials MATERIALS --source-commit COMMIT
python tools/distribution/package_release.py --stage STAGE --output PACKAGES \
  --base-url RELEASE_URL --source-commit COMMIT
```

An unreviewed platform profile or missing source is a release failure. A review
flag cannot bypass missing notices, hashes, source or runtime ownership checks.

## Optional receiver client and independent service

The base archive omits the SDRplay client. Its independent GPL toolbox links only
the BSL SoapySDR core and pinned SoapyRemote client; the upstream source archive
and `cmake/RemoteReceiver.cmake` accompany the package. The upstream server built
for tests is not installed in the application. Vendor APIs/drivers are not bundled
or loaded by this client. Rebuild or replace the independent service separately;
its own dependency and vendor terms remain applicable. See
[receiver services](docs/receiver-services.md) for setup and native-data limits.
