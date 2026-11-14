# Native macOS build and deployment acceptance

English · [简体中文](26-macOS原生构建与部署验收规格.md)

The supplied native target is Intel x86_64/macOS 14.3.1, not the hosted Apple-Silicon/macOS-15 release assumption. Build, tests, actual package and desktop workflow need separate evidence. The original acceptance did not publish a formal release or modify signing keys. Later Foreshadow branding changes need their own checks; a renamed path is not automatic reacceptance.

## Compatibility ownership

Shared-pointer publication is the change axis. Capture/background components own publishing; receivers/detection/recovery/UI retain their public snapshot contracts. SignalCapture::State, RecordedWaveformWorker and WaveformAnalysisSaveWorker use standard shared-pointer atomic free functions, matching the existing media approach, for libc++ lacking atomic<shared_ptr> specialization.

Every concurrent read/store/exchange stays atomic and sequentially consistent. Do not mix ordinary access, extend locks over acquisition/DSP/files, add platform/device/modulation-ID behavior to consumers, or mutate an object after publishing it read-only.

## Native toolchain and package

Use the existing LLVM 21 verified by an independent C++20 compile/run probe; Apple Clang 15 lacks required thread facilities. Qt 6.8.3 matches the target. Verify source/dependency archives, deployment target and every actual Mach-O minimum without changing system SDK/apps.

Run full Mac CTest, Cocoa wide/scaled/compact, six synthetic portable cases, save/reopen/location/evidence/byte export, and actual archive startup/modules/standalone toolboxes under Chinese/non-BMP paths. Audit every Mach-O dependency/minimum/runtime closure, relocation and local signature. Hardware capture/Apple Silicon/other OS versions remain separate unknowns. Cross-platform concurrency changes also require Windows/Linux capture/background/save and architecture regression.

Non-system toolchains declare complete dylib closure through `AMC_MACOS_EXTRA_RUNTIME_LIBRARIES` and notices through `AMC_MACOS_EXTRA_RUNTIME_LICENSE_FILES`. Application and external research provider install them at their own assembly points; generic relocation computes loader-relative references. System `/usr/lib`/`/System/Library` remain system. Dereference symlinks while distributing declared load names; no developer Homebrew dependency or runtime/provider-name branches in installer/UI. Audit all standalone-toolbox Mach-O too; new toolchains use the same declaration.

Distinguish file dependencies from search directories: bare `@loader_path`/`@executable_path` is valid LC_RPATH, not a missing filename. This must not admit absolute non-system references or absent dylibs.

Relocate before signing. Bundle owner recursively signs the application after all nested relocation/signatures; do not sign the main entry as a leaf while nested files remain unsigned. Standalone executables and adjacent libraries use explicit leaf mode and independent verification.

Power-report declares runtimes/notices through existing `resources`, for development and installation. Legacy resource-list installation and managed transactions copy the same complete declaration; generic installer never identifies library names. GUI test fixtures assemble actual external-tool declarations, not a guessed executable plus one directory. Keep controlled Toolbox Center; do not reopen arbitrary local import.

Distributable Mac tree is the app bundle plus external toolbox payloads. CMake's extra development bin/libexec copies are not a second portable app. Actual release tree still audits every Mach-O. Qt deployment searches installed `Foreshadow.app/Contents/Frameworks` (historically AMS.app) for the assembled runtimes, without developer SDK paths.

## Native UI and sustained evidence

File-range editing belongs to original/channel pages; model/window/recovery own their applied ranges with source support. Toolbar uses system small icons. Do not reapply layouts on scale/resize. Cocoa 640×480 verifies usable data height, reachable parameters and unclipped coordinates.

Generic complex evidence scrolls measurements/selectors/plots rather than compressing them into overlap. Plane selectors have bounded visible width with full dropdown values. PSK/OFDM compact checks scroll to actual visible metrics/chart; an independent provider's known visible point must click/locate precisely. Numbers or a PNG existing are insufficient.

Normal capture CTest retains two seconds. Explicit `signal_capture_test --sustained-seconds 180` runs one continuous 1 MS/s synthetic input, checking actual budgets, accepted/rejected counts and immutable frozen windows. It proves neither hardware losslessness nor other-rate performance.

The media GUI-stall contract waits up to five seconds for actual independent-fixture A/V playback, then compares published progress through a 300 ms UI-event pause. Fixed sleep cannot prove startup. A one-second delayed-start fixture checks that initialization delay is not mistaken for playback stall; stopped progress still fails. Do not skip/weaken continuity to qualify a platform.

References: [libc++ C++20/P0718R2](https://libcxx.llvm.org/Status/Cxx20.html), [Qt 6.8 platforms](https://doc.qt.io/qt-6.8/supported-platforms.html), [dyld expansion](https://github.com/apple-oss-distributions/dyld/blob/main/dyld/Loader.cpp).
