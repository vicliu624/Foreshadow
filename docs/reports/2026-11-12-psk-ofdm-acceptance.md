# PSK/OFDM reception, equalization and EVM acceptance

English · [简体中文](2026-11-12-PSK与OFDM接收验收.md)

Historical batch: 12 November 2026. The user initially deferred macOS; subsequent Intel Mac acceptance and deployment are recorded in [the native macOS report](2026-11-12-native-macos-deployment.md). The deferral below describes this earlier Windows/Linux batch, without overriding that later result.

## Scope and source identity

The request was to complete automatic PSK/OFDM synchronization, channel estimation, equalization and EVM, then replace the Windows/Linux review builds. Implementation followed specifications16,17,18,24 and [25](../25-psk-ofdm-and-evm.md).

`fbc99ae` introduced algorithms/protocols; `15e9df5` strengthened whole-configuration validation of ordered subcarriers and training bindings; `22fe498` and `7e0111c` added independent complex-plot click/location tests and corrected their completed-state setup. The actual review-package source was **`7e0111ce31fb9d6d4a4567121a728abbcdf54a1f`**, on `codex/waveform-lab`. Later report edits did not change production binaries. This was a review deployment, without a formal release or uploaded signed catalog.

## Implemented behavior

- BPSK/QPSK: Mth-power FFT carrier acquisition; known-preamble correlation; fractional timing and initial clock search; training-phase-slope carrier refinement; rectangular/RRC matched filtering; training-region effective-channel least squares and finite feed-forward equalization; held-out training checks; decision-directed data phase/timing tracking; candidate symbols/bits/bytes.
- OFDM: CP/repeated-training acquisition; integer/fractional carrier search and refinement; FFT anchoring inside the CP; active-subcarrier LS channel/noise estimates from two training repetitions; ZF or training-noise-regularized equalization; pilot common-phase/slope tracking; band-limited fractional FFT windows; deep-fade erasures. Subcarriers must already be ordered: sorting cannot silently rebind training/pilot values.
- EVM: RMS/peak error after equalization and phase correction, normalized by average reference power, with explicit known-data or decision reference. Training, pilots and invalid points are excluded from data EVM. Valid/excluded counts are displayed. Fewer than eight valid points, over20% exclusions or loss of lock make EVM unavailable. Data reference does not influence acquisition, channel or equalizer estimates. Low EVM does not validate an application frame.
- Generic workflow: recovery protocolv2 complex planes, groups/signed coordinates, generated references and scalar measurements; generic constellation/channel views and value tables; point click/input location; evidencev2 export, recovery-configurationv2 and outer analysisv8 persistence. v1 providers and older archives remain compatible. Plotting is bounded to16,384 points; full evidence/tables/EVM retain the complete data.

Each recovery provider owns algorithms, whole-parameter validation and English/Chinese resources. Only the recovery registry assembles them; GUI, workers and export contain no PSK/OFDM-ID behavior branches. An independent `independent.vector-plane` provider demonstrated measurement display, French-resource addition/English fallback, complex-point clicks, exact source location and configuration restoration through the same protocol.

## Independent numerical evidence

`coherent_recovery_contract` generates transmitter truth independently: RRC by numerical integration of the frequency-domain square-root raised-cosine response, OFDM by explicit inverse DFT/continuous-time subcarrier evaluation. Inputs include fractional arrival, carrier/phase offset, multipath, sample-clock error and noise. Tests recompute RMS/peak EVM point by point and check every valid payload decision.

Values below are the Linux baseline. Windows passed the same truth/tolerance checks, without requiring identical random-noise samples from different standard libraries.

| Truth/control | Verified result |
|---|---|
| BPSK/QPSK, rectangular/RRC; 128data symbols; 79.3Hz offset; fractional start/two paths | All four payload decisions correct; RRC BPSK/QPSK RMS EVM about4.442%/4.006% |
| RRC QPSK; 1024 symbols;+900ppm input clock;−163.1Hz offset |1024valid decisions correct; RMS EVM about11.958%; a result for this input, without general performance or ppm-accuracy guarantee |
| OFDM 64/16;32data symbols/1536data points;+400ppm;−1.27subcarrier offset; two paths |1536valid decisions correct; RMS EVM about1.694%; acquired offset about−1.270454subcarriers |
| Independent physical two-path frequency response |Normalized RMS channel error below1% after removing the uncalibrated common complex gain |
| OFDM dual notch |±16subcarriers remain unknown in every data symbol; 16 points excluded across eight symbols, without valid bytes or EVM contributions |
| Reference isolation |Known-data/blank reference does not change received complex points; incorrect reference is not fitted away |
| Negative/budget controls |Wrong training, noise/silence, gaps, cancellation, invalid parameters and independent resource-limit violations reject or retain invalid state; no fabricated zero-padded observation |

These are explicitly configured synthetic truths, without physical RF, blind-protocol or long-term-losslessness qualification. The effective channel is estimated under the analysis clock and declared pulse, rather than RF calibrated. Natural phase numbering and most-significant-bit-first byte mapping remain explicit.

## Platforms and actual packages

| Check | Linux | Windows |
|---|---|---|
| Full regression |102 checks: 99 passed,3existing SDRplay environment skips; final test additions also ran through native/packaged workflows |Final `7e0111c`:100 checks,99 passed,1audio-device skip |
| Native Qt |1600×1000,1.25-scale2000×1250,640×480; PSK/OFDM image sizes checked |Same three layouts; independent complex-point location passed |
| Real package |`release-qa/package-1794473727487872658` |`release-qa/package-1794473996777755500`; extracted desktop regression approximately114s |
| Package scope |Chinese/non-BMP paths, application/module discovery, independent toolbox transactions, native startup and extracted desktop regression |Same checks |
| macOS at this batch |Deferred by user; later report supersedes deferral |Deferred by user |

Application-package SHA-256:

- Windows: `446e87202d50690dd1252553a6ed27ba828cca0e0082d72d12510027a12ccf78`
- Linux: `e58aea3305a69544fda13017cce07a96575b7b9f338e026a3e418389351a899a`

Private evidence roots were `/home/vicliu/.cache/amc-waveform-01a1c9fc` and `C:/Users/vicliu/.cache/amc-waveform-01a1c9fc-windows`: numerical truth, source snapshots, build/CTest logs, three native layouts and real transactions. Private RF inputs were not committed.

## Failures and corrections retained

1. Linear fractional interpolation distorted long OFDM amplitude. Carrier-removed band-limited fractional sampling replaced it; independent continuous-time truth verified clock tracking.
2. A multipath CP peak could anchor the FFT late and leak the next symbol. Anchoring within CP lets training absorb phase slope; dual-notch tests prevent treating that leakage as valid carriers.
3. Eight initial Windows recovery/worker failures came from MSVC `/showIncludes` encoding inconsistent with Ninja. Old objects recorded zero header dependencies and mixed incompatible protocol layouts. UTF-8/compiler redetection plus rebuilding all292project objects made the same source pass. The LoRa object then recorded56dependencies, including recovery headers. Pinned downloads and third-party build trees were retained. References: [Ninja MSVC dependency protocol](https://ninja-build.org/manual.html), [upstream encoding discussion](https://github.com/ninja-build/ninja/issues/613).
4. One initial final-Windows toolbox-uninstall assertion failed with `Uninstall did not preserve the package outside the loading directory`. Log: `logs/native-7e0111c/windows-tests-first-run-uninstall-failure.log`. The same binary subsequently passed the complete100-check run. Cause was not established; this is not recorded as a repaired product defect. Real package transactions were checked separately.
5. The independent plot-click test initially omitted the host's completed/available state, leaving location disabled. The test now establishes and asserts availability, then checks exact source ranges at all three native sizes; source assertions were not relaxed.

## Review deployment and workflow

Both deployments came from accepted complete release archives and were compared file by file against the accepted extracted trees: 178 Windows files and118 Linux files. Both receiver modules loaded and research-provider startup verification passed.

- Windows: `C:/Users/vicliu/Apps/AMS-Review-PSK-OFDM-20261112-r2`; owned desktop shortcut updated; observed ordinary GUI PID 34680 resolved to this executable.
- Linux: `/home/vicliu/Applications/AMS-Review-PSK-OFDM-20261112-r2`; owned application-menu entry updated; Xorg: 0 GUI PID 114988 verified.
- Settings/events/toolboxes were isolated in the new roots. Earlier deployments/processes were retained. First launch chose the research workspace, then used saved layouts.
- Existing shortcuts were changed only after proving ownership. Formal installation and signing keys were untouched. A final Windows console print failed on GBK/non-BMP text after manifests,178file comparisons and probes had completed; manifest verification and UTF-8 output confirmed success without overwriting/repeating installation.

Six actual desktop-saved/reopened synthetic cases covered waveform/model, FSK, LoRa PHY, paired-code candidate repair, `05-QPSK同步均衡与EVM.amc-analysis.json` and `06-OFDM同步均衡与EVM.amc-analysis.json`. No private RF data was included. Portable-case archive SHA-256: `5f27ff9dbd64ecff92679a57f56ed6f3513cf5e77e0bf69d84b16f97ab9dda36`.

Review flow: Open analysis → inspect carrier offset/EVM/valid points → switch pre/post-equalization/channel planes → filter symbol groups → click and locate input → inspect candidate bytes → save/reopen → export evidence or known bytes. Complete advanced parameters remain in the owning recovery provider's right-hand panel.
