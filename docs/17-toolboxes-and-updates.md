# Controlled toolboxes and online updates

English · [简体中文](17-受控工具箱与在线更新规格.md)

## Product boundary

Foreshadow distributes applications/toolboxes through an official catalog, not arbitrary sources, a third-party marketplace or a drag-and-wire editor. Toolbox is an installation/version unit; module is a runtime unit. The host owns sessions, scheduling, recording and generic results; packages enter through their type protocols.

Receiver support and external research tools can be independent packages. Statically linked detection/display/research modules are labelled bundled and update with the application, not falsely advertised as replaceable binary plugins. Existing module selection remains available.

## Ownership

| Axis | Owner | Stable consumer / evidence |
|---|---|---|
| Official catalog | Signed catalog protocol/publisher | Update service/center; new entries do not edit installer/UI |
| Package type | Type adapter/registration | Generic transaction; independent test type installs without ID branches |
| Network | Transport adapter | Catalog/install service; local HTTP tests cover timeout/interruption/redirect/hash |
| Receiver | Receiver toolbox adapter | Enabled managed ID overrides bundled ID; disable masks package; uninstall restores bundled |
| Research | Research toolbox adapter | Declared resources/parameters/runtime enter generic host, including disable/uninstall |
| Application replacement | Platform helper | Wait for exit, switch, verify startup, rollback failed activation |

Separate catalog, transport, filesystem state, package types and UI. Consumers do not branch by toolbox/vendor/version ID. Text is resource-keyed.

## Identity and trust

Application/toolbox/protocol versions and source identity are independent. Semantic prereleases precede the matching final version. Platform includes OS/CPU and Linux baseline. Missing build, incompatible app range/protocol/dependency is visible and blocks installation. Installation records choose versions, not directory order. Installed/bundled/loaded/pending versions are distinct; running native modules are never unloaded.

The client fixes the official HTTPS catalog and Ed25519 public key; no arbitrary-source editor. Entries include versions/notes/download and expanded sizes/SHA-256/platform/dependencies/modules. Signature authenticates origin; hash checks content integrity.

Packages carry manifests/resources without install scripts. Verify catalog signature, archive hash, identity/version, paths, expansion size, duplicates and component structure. Reject escapes, invalid links and corruption. Offline import still matches trusted entries.

## Installation and activation

Use a writable per-version user store. Download temporarily, validate, extract into staging, type-check, then atomically commit package/record under a process lock. Interruption, hash failure, low disk or record-commit failure must preserve the current valid version. Settings/recordings are outside install roots.

Keep current/recovery versions; cache can be cleaned. Uninstall withdraws activation first, delaying cleanup of loaded files. External helpers replace applications after shutdown; running binaries are not overwritten. A new process must acknowledge readiness with matching credentials and remain alive. Failed startup restores prior activation/entry and relaunches the old version. Readiness occurs after the initial event loop/layout; writing an acknowledgement and immediately exiting is failure.

Downloading/checking can be background/cancellable with phase/progress/error. Actual changes check active listening, recording, exports, research and device queries. An atomic commit already in progress completes safely after cancellation. Unwritable system roots produce a usable user-install path, not silent elevation. CPU overload is not package incompatibility.

Center pages show official/installed/updates/software, provenance, current/latest/pending, reasons, dependencies, notes and results. Actions include install/update/enable/disable/uninstall/cache/offline import. Failed checks leave offline use intact. Network honors system proxy.

## Resources and platform packaging

Mac language data belongs in Contents/Resources, not the code-signed executable directory. Provider `catalog_roots` candidates resolve within the installed package/app boundary. Schema-1 resource declarations assemble complete content; directory shape is not a receiver-ID rule. Package-relative Mac resources declare minimum host 0.2.6; former adjacent-root compatibility remains.

Windows receiver private runtimes follow provider build declarations and ship adjacent to modules; developer PATH is insufficient. Mac dependencies also come from registered declarations. Native activation leases plus current/previous versions constrain cleanup.

Linux uses user-root stable launchers and `.desktop`; Windows GUI/CLI launchers and desktop shortcut; Mac user Applications launcher bundle. Launchers do not need Qt; CLI forwards arguments/stdout/exit status. New visible entries use Foreshadow. Existing `signalscope` commands, state roots, trust source and package IDs remain compatible.

## CI and formal release operations

Source/catalog/assets use the repository Releases. Binaries exclude private keys/tokens/private files. Daily builds reuse local caches and trusted self-hosted runners for pushes/same-repository PRs; untrusted fork PRs do not execute there. Hosted platform matrices are explicit release validation only; creating an accepted tag does not rebuild them.

WSL service installation alone is insufficient for unattended lifetime: [Microsoft documents that systemd services do not keep WSL alive](https://learn.microsoft.com/en-us/windows/wsl/systemd). Verify real CI after closing build terminals with suitable host lifetime management. Since 2026-11-24 daily CI uses the native Debian 13 `foreshadow-debian13-x64` runner under a dedicated Linux account/systemd service. Its current network route depends on a Windows proxy tunnel; service lifetime does not establish network availability after that Windows host shuts down.

`tools/distribution/package_release.py` reads tested CMake installation and emits application, receiver and power-report packages plus unsigned entries, with exact sizes/hashes/platform/source identity. Platform jobs never access signing keys. External research is a separate toolbox; bundled module choices remain in module settings.

Manually dispatch **Release platform builds** with `release_tag`, `platform`, and a checksum-pinned HTTPS `compliance_bundle_url` / `compliance_bundle_sha256`. The default `reviewed` selection builds Windows x64 and Debian 13 amd64/arm64 for 0.2.13; `all|linux|windows|macos` remain available for separately prepared release scopes. Every material inventory must match the frozen workflow commit. Debian builds additionally compare installed SDK source-package identities against the reviewed `.dsc` files before compiling. Tag/version/asset URL must agree. All required platforms must pass frozen-source CTest, actual archives extracted under Chinese/non-BMP paths, component loading and distribution package transactions before creating the immutable v-tag. Portable archives come from the qualified base-package composition, with optional toolboxes tested separately.

Publisher reviews/downloads builds, then runs `tools/distribution/sign_catalog.py --entries ... --key ... --output catalog.json` with an external Ed25519 private key; `--previous` validates/merges the preceding catalog and sequence must increase. Tool verifies public-key match, hashes and signature. Upload packages/catalog to the formal release only after this review. Keys stay outside Actions/Git/packages/remote deployment hosts.

**Release media deadline diagnostic** is manually invoked formal-release diagnostics, not daily CI. It compares condition-variable, relative/Mach and absolute waits independently of codecs. Retain native logs; host timing blockage and functionality are separate conclusions.

## Acceptance

Test two toolbox versions through install/upgrade/disable/uninstall/restart, generic independent types, signatures/downgrades/dependencies/platform/offline/cancel/path escapes/disk/concurrency/task conflict; then actual application startup, failed-start rollback and saved-profile restart. A timing skip is explicit per invocation and must not skip later decoding/UI/dependency checks. Only report deployed after checking actual running binaries/modules/configuration.

Operational CLI `signalscope-toolboxes check|list|install ID|enable ID|disable ID|uninstall ID|rollback ID` shares the GUI trust/compatibility/transactions. Managed receiver overrides bundled; removing the managed update restores the bundled implementation. Changes involving native code require restart.

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
