# Online updates and controlled-toolbox release acceptance

English · [简体中文](2026-11-09-在线更新与工具箱发布验收.md)

Historical0.2.6release record. Scope: official catalogs and toolboxes, without arbitrary sources, account marketplace, third-party uploads or a graphical wiring graph. Toolbox Center supplies catalog/installed/updates/software pages; install/update/enable/disable/uninstall/rollback/offline import/cache cleanup and startup/manual checks. Built-ins are accurately marked as shipped with the app.

## Protocols and extension boundaries

Catalog trust, network transport, generic transaction, package type, runtime discovery and activation own distinct rules. Independent providers, a third receiver, resource increments and a test package type proved extension without IDbranches in UI/CLI/sessions. Constraints are inAGEN TS/specifications16–18.

Fixed official entrance: `https://github.com/vicliu624/AMC/releases/latest/download/catalog.json`. Ed25519signatures and exact SHA-256/expanded-size/platform/version/dependency/component validation precede installation. Locks, staging and atomic state commit protect transactions; current/recovery versions and process leases delay cleanup. The helper waits for the old process, validates new-process credentials/survival and restores entry/state on failure. Settings/raw recordings remain outside application install trees.

## Tests and platforms

Frozen source `9e154613ae49553603ab147a7a75aa536b43cf8a`: local45/45; target42 passed/3skipped due deterministic Soapy mocks conflicting with actual vendor discovery. Those skips are not hardware passes; actual RSP was checked separately. Installed CLI capture/replay, GUI replay, independent-receiver extraction and before/after discovery passed.

RSPdx-R2 at100 MHz/10 MS/s/10 MHz, all detectors,16configured workers, ten seconds: 100,000,000 pairs each in input/recording/main/auxiliary, zero software rejects and visible FM candidates. This does not supersede the60second0.90%LoRa loss in the [pipeline report](2026-11-09-fm-and-realtime-pipelines.md).

[Release run42621529629](https://github.com/vicliu624/AMC/actions/runs/42621529629) required build/full applicable suite/extracted execution/real transactions before catalog signing:

| Platform | Regression | Real transactions/extracted replay |
|---|---|---|
| macOS 15 ARM64 |45 passed|Passed|
| Ubuntu 24.04 x86-64 |45plus7legacy|Passed|
| Ubuntu 24.04 ARM64 |45 passed|Passed|
| Debian 13 x86-64 |45 passed|Passed|
| Debian 13 ARM64 |45 passed|Passed|
| Windows  x86-64 |43 passed|Passed|

Virtual-display wide layout used1920×1080; native archives used available-screen compact layout with1.25scale/replay. Separate1024×768 X11 replay passed; actual target FM rows were visible. macOS Mach-O/link audits and local ad-hoc signing passed, without Apple notarization or first-download Gatekeeper claims.

Actual Windows archives exposed code-page bugs. Discovery/diagnostics, atomic-state filenames, resources and environment now use native-path/UTF-8boundaries, with wide-character environment APIs. `distribution_package_test` launches the whole app from Chinese/non-BMPpaths and checks preinstalled and independent before/after module loading; resources separately use Unicode environment values. These became engineering requirement14/specification 16.

Windows dependency caches key on Runner image/vcpkgcommit and save after dependency success, surviving later application failures; final run hit the cache. Daily builds remained self-hosted.

## Deployment and host environment

Target `vicliu@192.168.31.42`, Debian 13 x86-64. [0.2.6](https://github.com/vicliu624/AMC/releases/tag/v0.2.6) published30assets: 24application/toolbox packages across six platforms, signed catalog and complete archives. Every GitHub SHA-256 matched reviewed local output. Catalog sequence1794230473retained0.2.3/0.2.5recovery entries,32records total.

The target downloaded/verified/preflighted/installed application, RTL-SDR, SDRplay and power-report0.2.6through the fixed official entrance. The old process ran until activation. Independent helper returned `applied`; PID 1092330ran `packages/org.amc.ams/0.2.6/bin/signalscope-gui`, source9e154613…. GUI SHA-256 `32e5de659f5932016e92cfeb9a1c00382f61584fac8055016fe75dec8e654400` matched the formal package.

Stable CLI reported0.2.6; desktop used stable GUI launcher; both managed receiver modules loaded0.2.6. `~/.config/SignalScope/SignalScope.conf` and current/recovery payloads remained; old manual installs were removed. Formal installed GUI was rechecked with isolated configuration and the ten-second100million-pairrun, without changing user workspace/recordings.

Historical cleanup removed about1.736GBunusedDockerobjects and1.085GBbuildcache, retaining running containers/volumes. Later cleanup was confined to this task's old/rebuildable assets; unrelated disk changes were not attributed to it.

The temporary deployment route was closed after the actual official online upgrade. The user's systemproxy127.0.0.1:7897remained; the final process had no temporary-proxy environment. Update check in that environment returned structured `network`/`Connection closed`/status0and displayed failure accurately. No other persistent proxy was added. Offline installed capabilities and signed offline import remained usable.

The repository was made public under the user's decision. Daily CI used local self-hosted Runner; hosted Runner was for explicit releases. Private signing keys remained outside the repository, Actions, target and packages. Actual RF files stayed out of Git/assets.
