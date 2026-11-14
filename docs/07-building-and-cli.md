# Building and command-line operation

English · [简体中文](07-构建与命令行操作.md)

This is the current guide. The former LoRa-only rate limits, fixed 1 MHz recommendation, and “no desktop entry” statements are historical. See the [README](../README.md) for the full product overview and [source map](../src/README.md) for build ownership.

## Prerequisites

Use CMake 3.20+, C++20 and Threads, OpenSSL Crypto 3.0+, and libarchive. nlohmann/json 3.12.0 and KissFFT 131.1.0 are hash-pinned FetchContent inputs. FFTW/VOLK are optional acceleration paths. A missing hardware dependency must produce explicit provider unavailability, not simulated hardware.

Desktop needs Qt Widgets, Network and LinguistTools (Qt 6.2+ on non-Mac, 6.8+ on Mac). Multimedia supplies audio; SerialPort builds the Pager host utility. Hardware SDKs belong to receiver modules, not the stable host. Full TV builds assemble pinned native dependencies and need shell/Make/NASM on x86. Read [third-party dependencies](../third_party/README.md) before overriding them.

Use the [release workflow](../.github/workflows/release.yml) as the complete platform dependency recipe and the [self-hosted workflow](../.github/workflows/desktop.yml) for daily CI. Daily CI must not be moved to hosted runners.

## Configure, build, test, install

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIGNALSCOPE_REQUIRE_GUI=ON -DSIGNALSCOPE_REQUIRE_AUDIO=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
cmake --install build --component SignalScope --prefix "$PWD/stage"
```

For Visual Studio use `-G "Visual Studio 17 2022" -A x64`, a matching Qt MSVC prefix and dependency toolchain; add `--config Release` to build/install and `-C Release` to CTest. The commands in the README illustrate this; the Qt prefix alone does not provide OpenSSL/libarchive or TV build tools.

For Mac set the intended compiler/runtime and `CMAKE_OSX_DEPLOYMENT_TARGET`. The accepted Intel baseline uses LLVM 21.1.8 / Qt 6.8.3 / target 14.0 on macOS 14.3.1. Declare non-system runtime closure and notices through `AMC_MACOS_EXTRA_RUNTIME_LIBRARIES` and `AMC_MACOS_EXTRA_RUNTIME_LICENSE_FILES`. Do not rely on developer Homebrew paths at launch. See [specification 26](26-native-macos-acceptance.md).

`SIGNALSCOPE_GUI=OFF` omits desktop; `SIGNALSCOPE_TELEVISION=OFF` intentionally omits TV. `SIGNALSCOPE_REQUIRE_GUI`, `SIGNALSCOPE_REQUIRE_AUDIO`, `SIGNALSCOPE_REQUIRE_RTLSDR`, and `SIGNALSCOPE_REQUIRE_SDRPLAY` prevent silently incomplete requested builds. `SIGNALSCOPE_FFMPEG_ROOT` is a development override, not proof of codec equivalence. `SIGNALSCOPE_LEGACY_TESTS=ON` enables retained Python tests; ordinary production DSP is native.

For offline builds supply verified sources through `FETCHCONTENT_SOURCE_DIR_JSON`, `FETCHCONTENT_SOURCE_DIR_KISSFFT` and the relevant pinned native dependency overrides. Keep source identity/hash provenance. Do not downgrade a dependency merely because its library/codec name matches.

## Install verification

Windows/Linux GUI is `stage/bin/signalscope-gui[.exe]`; Mac is `stage/Foreshadow.app` with CLI under `Contents/MacOS`. Internal install component `SignalScope` and executable names remain compatible. `ResearchExamples` separately installs the external research toolbox payload used by package checks.

```sh
cmake --install build --component ResearchExamples --prefix "$PWD/stage"
cmake -DAMC_CLI="$PWD/stage/bin/signalscope" -P tests/check_packaged_receivers.cmake
cmake -DAMC_STAGE="$PWD/stage" -DAMC_TEST_BIN="$PWD/build" -P tests/check_desktop_package.cmake
python tools/docs/check_docs.py
```

Adjust CLI/test paths for Windows multi-config or the Mac bundle. Package acceptance must use a real archive extracted under Chinese/non-BMP paths. Check modules, complete resources/runtimes/notices, standalone toolbox transactions, actual startup and saved-layout restart. `tools/distribution/package_release.py` creates application/receiver/research packages and unsigned catalog entries from a tested install; signing/publication is a separate externally keyed release operation.

## CLI surface

Use `signalscope --help` and `--build-info` from the actual build. Common commands:

| Group | Commands |
|---|---|
| Receivers | `receiver-plugins`, `devices`, `probe`, `parameters`, `recommend`, `validate`, `configure` |
| Raw data | `synth`, `capture`, `inspect`, `export`, `replay` |
| Detection | `detect`, `scan`, `monitor`, `replay-events` |
| Storage | `store-snapshot`, quota-aware `monitor` |
| Audio | `fm-audio` |
| Distribution | `signalscope-toolboxes check|list|install ID|enable ID|disable ID|uninstall ID|rollback ID` |

Receiver commands accept `--receiver ID`, device index and supported serial/antenna selectors. Vendor settings use `--settings JSON`. `probe` provides capabilities; `parameters` describes controls; `validate` returns hardware/analysis issues and a nonzero exit for invalid combinations. `configure` applies, confirms startup where required and returns requested/actual values without saving I/Q. Hardware state need not persist after close; acquisition configures again.

CLI frequencies are Hz; GUI `--frequency` is MHz. Workers accept 1–32, while actual recommendations/coverage depend on the build/provider and host. Recording/export destinations must be new, their parent writable. Unsupported gain/rate/format is an error. Ctrl+C requests orderly cancellation; errors retain partial/error metadata.

```sh
signalscope devices --receiver rtlsdr
signalscope probe --receiver sdrplay
signalscope validate --receiver sdrplay --purpose capture --rate 10000000
signalscope configure --receiver sdrplay --rate 6000000 --gain auto --antenna 'Antenna B'
signalscope synth --output tone-demo --rate 2000000 --seconds 0.1 --tone 25000
signalscope inspect --input tone-demo/capture.sigmf-meta
signalscope replay --input tone-demo/capture.sigmf-meta --speed 0
signalscope scan --input capture-001/capture.sigmf-meta --modulations all
signalscope detect --input capture-001/capture.sigmf-meta --bw 125000 --sf-min 7 --sf-max 12 --search-width 1000000
signalscope replay --input capture-001/capture.sigmf-meta --start 1 --duration 0.5 --speed 0 --output slice-001
signalscope monitor --input capture-001/capture.sigmf-meta --output replay-events --seconds 0
```

`replay --speed 1` is real time and `0` is fastest; monitor file input has its own replay pacing. Software replay never transmits. Slices retain parent sample offsets, not a newly invented RF arrival time. `scan` emits JSON lines with source sample positions; a candidate is not a payload decoder result.

## Desktop operations and diagnostics

```sh
signalscope-gui --language en --settings-dir review-profile --output review-events
signalscope-gui --replay capture-001/capture.sigmf-meta
```

`--settings-dir` isolates preferences and, absent an explicit distribution-root override, managed packages. Normal user settings/data retain historical SignalScope roots. `--language` accepts system or an installed language tag. GUI module parameters belong to their provider panels.

For native synthetic examples, build tests and run `waveform_recovery_workflow_test --export-review-fixtures NEW_DIR`. It saves/reopens six cases. Physical-input acceptance is separately built with `SIGNALSCOPE_HARDWARE_ACCEPTANCE=ON` and manually invoked. Normal CTest does not open real receivers.

Queue and store options are described in [continuous monitoring](08-continuous-monitoring-and-event-storage.md). Do not interpret software counts, nominal ADC bits, configuration acceptance or file size as calibrated RF quality/hardware loss proof. Project licensing remains an explicit open decision; retain dependency notices and source/relink material.

## Independent receiver services (0.2.12)

The base application and optional SDRplay service-client toolbox are separate
delivery units. Vendor API/driver loading stays in the independently installed
receiver service. Provider-owned connection descriptors and request protocol 2
carry the address before enumeration; UI/sessions/CLI remain generic. The pinned
BSL SoapyRemote client needs SoapySDR 0.8.1 and supports the offline source override
`FETCHCONTENT_SOURCE_DIR_SOAPYREMOTE`. Only declared bundled providers enter the
base archive; raw CMake installation is a staging tree, not a qualified base
archive. See [receiver services](receiver-services.md) for SDRconnect assessment,
setup, protocol, continuity and license boundaries.
