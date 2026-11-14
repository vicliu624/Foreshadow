# First acquisition and environment record

English · [简体中文](2026-11-03-首次采集与环境记录.md)

Historical observations,3 November 2026, Asia/Shanghai. Target `vicliu@192.168.50.92`: Raspberry Pi Compute Module4Rev1.1, four Cortex-A72 cores, approximately7.6 GiB RAM, Debian 13.7trixie/aarch64, kernel7.1.4-v8+, CMake 3.31.6, G++14.2, librtlsdr2.0.1.

USB identity was0bda: 2838, manufacturer HackerGadgets, product AIO_V2 Ext. The driver enumerated an R820T/type5 tuner; that enumeration neither proves nor disproves the board's physical chip marking. Frequency range, HF, passband and sensitivity were not qualified.

## Actual first I/Q

Both captures came from actual USB input, without asking the user to transmit LoRa and without known-LoRa labels. Center470.5 MHz, manual gain19.7 dB, startup discard0.1s.

| Measurement |2 MS/s|2.4 MS/s|
|---|---|---|
| Target directory |`~/signalscope-dev/recordings/aio2-2m-first`|`~/signalscope-dev/recordings/aio2-2p4m-first`|
| Saved complex samples |20,000,000|24,000,000|
| Raw bytes |40,000,000|48,000,000|
| Driver return |0|0|
| Queue high-water blocks |1|1|
| Watchdog timeout |No|No|
| Full-scale endpoint clipping count |0|0|
| Acquisition-through-close wall time |10.143s|10.117s|
| Whole-band digital mean power |−43.0364 dBFS|−42.8913 dBFS|

Original-program offline replay agreed on sample count, power and DC statistics. Whole-band power is not LoRa signal strength or evidence that LoRa was found.

Raw SHA-256:

- 2 MS/s: `9b1d8e73975c2b27396147f0c29016aa07da5ea7669c71886ee288161e8af047`
- 2.4 MS/s: `31067aac31ed7cc42577f6466184f781b968200f542d3e6b2163116c4159a797`

Setting rate before frequency initially produced `PLL not locked`. Setting frequency first then rate, followed by a separate two-second actual capture, retained4,000,000 samples without that warning and replay agreed. This supports an initialization-order effect, without substituting for known-reference RF/frequency qualification.

## Continuity and software limits

An approximately15-second `rtl_test -s 2000000` run stopped with SIGINT reported minimum loss2ppm/at least160 bytes. Timeout's signal termination was not an application crash; the reported loss remains a failure boundary. Ordinary I/Q has no hardware sample sequence, so correct file length and driver return0 do not prove absence of unknown USB gaps. Startup/steady-state loss and sustained joint load still required separation.

Windows MSVC Release passed3/3 tests; target GCC Release passed3/3in about1.59s, linked against actual librtlsdr. The detector then provided repeated-chirp CSS candidates only, without LoRa SFD validation, sensitivity or complete UI. This was not complete MVP acceptance.

## Historical storage cleanup

The user explicitly requested deleting old experiment data without migration. After absolute-path/open-file checks, old `~/oneseg-rf-validation`, `~/OneSegView-Runs`, `~/.local/share/OneSegView/validation`, npm/pip/Electron caches, extracted QQ update ZIP and OneSegView dependency-deb cache were removed. This records completed history and is not a new cleanup instruction.

Final root free space14,703,337,472 bytes/about13.69 GiB (`df -h`:14G),50%used, versus3,361,673,216 bytes initially: about10.56 GiB added free space, with some new build/capture files created meanwhile. `/var/swap` was the active zram writeback backend on `/dev/loop0` and retained. No `apt autoremove` or application uninstall ran. New SignalScope recordings, approximately92 MiB, were preserved.
