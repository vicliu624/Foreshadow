# Foreshadow rebrand, plugin documentation and review maintenance

English · [简体中文](2026-11-13-foreshadow-maintenance.zh-CN.md)

Date: 13 November 2026, Asia/Shanghai. Frozen software source: `d7cfd271c1ee19e022051676fac48c545dab7381`. Documentation/checker/CI changes follow that binary source. [Structured evidence](2026-11-13-foreshadow-maintenance-proof.json) records the actual package/deployment identities. This was local review maintenance, without a formal release/tag/catalog publication.

## Product and architecture description

The display brand is **Foreshadow**. Window/CLI descriptions, resources, bundle assembly, update shortcuts and release archive labels were reconciled. Public commands/namespaces, `SignalScope` state roots, `signalscope:*` metadata, package IDs such as `org.amc.ams` and saved-analysis suffixes remain compatible. Historical artifact names/hashes retain their original dates.

The user identified that plugin importance was understated. README now leads with a plugin-driven workbench, maps each capability to its owner and links the bilingual [plugin guide](../plugins.md). Receiver, detection, display, live research, model, recovery and media providers deliver domain behavior; shared protocols/DSP, sessions, storage, workspace and distribution connect them. Runtime receiver modules, compiled built-ins and external snapshot executables are described separately. Toolbox is an installation/version unit, without an implication that every registered implementation is independently downloadable.

## Documentation and screenshots

The inventory contains69English/SimplifiedChinese pairs (138Markdown documents), with English defaults and reciprocal navigation. Current requirements/protocols/user/build/source guidance were reconciled; historical designs/decisions/diagnostics remain dated. Four long historical records gained organized English chronologies retaining material positive/negative/performance evidence. Earlier “Mac unavailable” or “PSK reference only” stages are distinguished from later acceptance.

README/gallery contain15English native screenshots, all captured on the receiver-connected Debian 13workstation. [Image provenance](../images/provenance.json) identifies source, dimensions and SHA-256. Actual RSPdx-R2 input/settings/FM running-state captures are separate from six explicitly synthetic research cases. Television plays the repository'sCC0synthetic TS, with muted output; local antenna-to-program reception is not claimed. Panel arrangement used ordinary workspace controls. Historical Mac images remain in their dated reports.

`tools/docs/check_docs.py` checks inventory/default language, counterparts, navigation, repository-local links, heading anchors and referencedPNGheaders/dimensions. It is called from the existing self-hosted CI. Separate temporary fixtures exercise rejection of missing counterparts and broken links. Markdown tables and whitespace are checked alongside the final inventory; this guard does not establish translation equivalence or native application behavior.

## Windows/Linux results and actual review deployment

| Check | Windows | Linux |
|---|---|---|
| Full CTest |100total: 99 passed,1existingaudio skip,0 failed |102total: 99 passed,3 SDRplay mock-isolation skips,0 failed |
| Real package |Application/receivers/research transactions, Unicode/non-BMP paths, discovery/startup and extracted desktop suite passed |Same checks passed |
| Complete archive comparison |178files matched accepted extracted tree |118files matched |
| Review root |`C:/Users/vicliu/Apps/Foreshadow-Review-20261113-r1` |`/home/vicliu/Applications/Foreshadow-Review-20261113-r1` |
| Entry |Desktop `Foreshadow Review` |Application menu `Foreshadow Review` |
| Launch language |Explicit `--language en` |Explicit `--language en` |
| Observed launch PID |7808 |183177 |

The package/deployment JSON retains exact archives, hashes and source identity (Linux package metadata uses the matching shortcommit `d7cfd27`). Both receiver modules and live-provider discovery passed after installation. Six portable analysis cases have English filenames and bilingual review instructions; new settings/events/toolbox state are isolated. Old application payloads were retained. Launchers have no screenshot-exit or repeatedly applied workspace-test options. PID values are observations, not permanent identifiers.

A Windows negative-plugin test exposed an actual bad-image modal for a deliberately corrupt optionalDLL. The receiver loader now temporarily uses thread-local `SEM_FAILCRITICALERRORS`, restores the prior mode and reports the existing generic rejection, without device-IDbranches or a larger timeout. Focused tests and the final complete suite passed. [Microsoft's loader documentation](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw) and [thread-error-mode contract](https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setthreaderrormode) explain the relevant system behavior. Original failed logs remain in the private task cache.

## New Mac qualification: failed, not accepted

The renamed Intel Mac source built successfully, but its first102-test run had94passes/8failures. Serial rerun of those eight passed three and still failed five: `gui_replay_smoke`, `media_playback_contract`, `dtmb_tv_provider_contract`, `dtmb_tv_actual_media`, `fm_playback_recovery`.

Observed output includes `AudioOutputUnitStart`error35/`Qt::ApplicationInactive`, missing media progress and timed workflows. Cause has not been established; this is not labelled a proven product correction or an external-platform skip. No thresholds/assertions were weakened. The new package/deployment gate was not advanced after the prerequisite failed. The prior accepted Mac review deployment remains intact; its earlier102/102result belongs to source`b06c3b5`and does not qualify the renamed bundle.

Private Mac logs are under `/Users/liuweikai/.cache/amc-waveform-01a1c9fc-macos/logs/`, including `foreshadow-tests-first-failure.log`, `foreshadow-LastTest-first-failure.log` and `foreshadow-focused-serial.log`. Windows/Linux logs and QAarchives remain in their existing task caches. Credentials/private keys/private RF inputs are excluded from repository artifacts.

Verified here: documentation/link/image structure, Windows/Linux source regressions/real packages/review deployment and receiver-workstation screenshots. Unverified or failed: renamed Mac package acceptance/deployment, latest-branchAppleSilicon, broadOScoverage, subjective audio, calibrated RF performance and long hardware-lossless operation. This maintenance does not extend earlier physical acceptance beyond its declared inputs/duration.
