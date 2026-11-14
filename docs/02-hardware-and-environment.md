# Hardware and environment investigation

English · [简体中文](02-硬件与环境调查.md)

**Historical investigation baseline, 2026-11-03.** Later receiver capabilities and platform qualification are in specifications 13–15/26 and dated reports. Early rates/search widths here are investigation candidates, not current product limits.

## LoRa wideband investigation additions

Initial scope was uConsole+AIO2 short-burst LoRa, without LR2021 as a prerequisite. Required observations: joint capture/display/detection/write load at 2.0/2.4 MS/s; antennas, clock error, filter edges, DC and strong signals in candidate 470–510 MHz; actual coverage of an initial 1 MHz center-search region.

Full-signal passband must cover center search + maximum BW + both guard margins. A 1 MHz search with BW 125/250/500k requires 1.125/1.25/1.5 MHz before guards. Bias tuning/DC processing must not silently mask a center hole. Record controlled transmitter frequency/BW/SF/preamble/pacing/stop independently; unknown-frequency detection must not receive those answers. Budget ring history for format/rate/detection latency/backlog and record disk quota/external storage. Requirements are not prefilled measurements.

## Observed status

Target: ClockworkPi uConsole / Raspberry Pi CM4 / HackerGadgets AIO V2. On 2026-11-03 the device at `vicliu@192.168.50.92` reported Debian 13.7/aarch64/CM4 and USB AIO_V2 Ext. Short 2.0/2.4 MS/s capture/replay passed; see [first capture](reports/2026-11-03-first-capture.md). Full tuning range/HF/effective passband/settling/long continuity were not established.

## Read-only environment baseline

Record environment first; missing tools/permissions remain explicit. Check distribution/repos before installing anything. Output can contain serials and should be redacted for public sharing as needed.

```sh
date -u
uname -a
cat /etc/os-release
getconf LONG_BIT
lscpu
free -h
df -h
lsusb
lsusb -t
lsmod
command -v rtl_test
command -v rtl_sdr
command -v cmake
command -v c++
command -v pkg-config
cmake --version
c++ --version
pkg-config --modversion librtlsdr
```

Check kernel USB enumeration/reset/disconnect/DVB occupancy; dmesg may require permissions. Do not unload modules/edit blacklists without authorization. Inspect local `rtl_test -h` / `rtl_sdr -h` before sampling; if supported, run `rtl_test -s 2000000` and `rtl_test -s 2400000` separately, recording duration/errors and stopping normally. One capture process at a time. Also test tuning and display/write load. No reported error does not establish absence of unknown losses.

## Required facts

| Area | Record |
|---|---|
| USB/driver | VID/PID/name/actual tuner/topology/serial, librtlsdr origin/version/vendor branch and occupancy |
| Tuning/HF | Driver claims vs applied tuning vs measured reception; actual HF path/mode/driver, leaving unknowns explicit |
| Samples/gain | Requested/actual rate, CU8 ordering/blocks/continuity, discrete/auto gain and transition behavior |
| Settling/passband | Request-to-valid-input delay, discarded samples, edges/DC/guards/overlap |
| Clock | PPM capability and a calibrated reference, not an arbitrary carrier treated as standard |
| Resources/display | Idle/load CPU/RAM/temperature/throttling/storage/write speed/resolution/scale/display server/Qt/input |

R860 model alone cannot establish HF or band-edge performance. Without a known reference/source, accepted tuning is not reception quality.

At CU8 2.0/2.4 MS/s: 4.0/4.8 MB/s, 14.4/17.28 GB/hour decimal excluding overhead. Complex float raises flow to 16.0/19.2 MB/s. Ring = rate × bytes/pair × seconds; use this for pre-trigger/caps/reserve/copy budgets.

## Report requirements

A resulting dated report records hardware/system/kernel/architecture/operator/date, exact tool/driver/dependency origins, commands/logs/missing permissions, claimed/measured/unverified facts, load-duration-error-continuity matrix, settling/guards/clock/recommendations and resource/display conditions. Explicitly state whether real-time development prerequisites are met. File/synthetic/offline work can proceed without hardware, but hardware qualification stays unverified.
