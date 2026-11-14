# 821 MHz reception migration request

English · [简体中文](10-821MHz接收迁移需求.md)

**Historical request confirmed 2026-11-04.** At that time it superseded the 478–479 MHz LoRa operating target; requirements did not imply implementation/deployment. It is retained design history, not a claim that today's software includes LBJ/POCSAG recovery.

## Target and receiver chain

Move uConsole+AIO2 observation near821 MHz, specifically821.2375 MHz for LBJ reception research. No 478/821 alternating scan or simultaneous two-band requirement. Preserve prior LoRa/recordings/replay/data, without making478 MHz a parallel task.

Dechirp+FFT LoRa structure cannot identify LBJ. The requested separate chain included channel extraction, FSK discriminator, bit timing, POCSAG sync and BCH/parity.1200 bit/s was an unverified starting hypothesis, requiring independent truth and actual input.

[CR_LBJ_Receiver](https://github.com/liujitong/CR_LBJ_Receiver) describes821.2375 MHz,960 kS/s→48 kS/s, FM discriminator and BCH(31,21). This is a design reference, not evidence of local reception/coverage. UI must distinguish acquisition center, target channel, sampled span and actual detection bandwidth; measure rate/filter rather than call whole span signal BW.

## Evidence levels

1. RF activity: energy/burst near target only.
2. POCSAG candidate: matching sync but insufficient checks/continuity.
3. POCSAG checks passed: valid words, BCH/parity, corrections and raw words; still not automatically LBJ.
4. LBJ application parsed: only after verified field/length/rules may train/speed/kilometer fields appear; unsupported/unconfirmed fields blank.

Retain spectrum/waterfall/digital strength/time/evidence/record/replay, with dBFS rather than uncalibrated dBm. Never label these events LoRa/SF/chirp.

## Storage/migration/acceptance

Use bounded RAM pre-trigger and disk quotas, not continuous daily I/Q. Default automatic retention was to require protocol validation; energy/unverified candidates only bounded summaries. Manual research capture has explicit duration/size. End the old478 MHz session and mark interrupted soak due to task change, not one continuous acceptance. Preserve old recordings; new events use independent directory/mode metadata. No RF transmission required.

Offline FSK/POCSAG tests first cover sync/checks/polarity/CFO/noise; then actual AIO2 tuning/continuous input/UI/bounded storage. If no valid local message, report that honestly; synthetic/CW is not environmental LBJ.

Recorded 2026-11-04 status: installed receiver still LoRa at478.5 MHz/1.75 MS/s. Only migration scope was confirmed; 821 MHz mode, POCSAG detection and deployment were not completed. Current implemented recovery methods are listed separately in the README.
