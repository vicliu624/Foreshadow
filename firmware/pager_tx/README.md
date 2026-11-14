# Pager serial-controlled low-power LoRa and GFSK firmware

English · [简体中文](README.zh-CN.md)

This is independent test firmware for the LilyGO T-LoRa-Pager SX1262 connected to the user's COM11. It uses Arduino ESP32 and RadioLib7.4.0, without Trail Mate business code. It has no display UI and does not initialize Wi-Fi, Bluetooth or GNSS. Building this repository does not flash or transmit automatically.

## Behavior and limits

- Boot remains idle, without automatic transmission, loops or restoration of a transmit job from NVS.
- Default nominal chip power is −9dBm; serial commands allow −9…0dBm. This is a setting, not measured radiated power.
- Each `TX` sends one8–240byte payload. Requests with estimated airtime over one second are rejected before transmission.
- Starts are separated by at least two seconds and ten times the estimated airtime.
- Transmission is asynchronous. `STOP` interrupts it; a watchdog handles a missing completion interrupt.
- LoRa uses an explicit header and two-byte CRC. `TX_DONE` reports transmitter-chip completion, without proving reception.

## Board wiring

SPI uses SCK35, MISO33 and MOSI34. SX1262 uses CS36, DIO1/IRQ14, RESET47 and BUSY48. Display, SD and NFC chip selects on the shared SPI bus are set high first. I²C uses SDA3/SCL2; XL9555 address0x20 changes only port0 bit3 for LoRa power. TCXO voltage is1.6V. The board and radio initialization were checked by serial readback in the original test.

## Serial protocol

115200baud, ASCII commands terminated by newline, JSON-line replies. Parameters are not persisted; reset returns to idle defaults.

```text
PING
STATUS
SET 478321000 250000 11 5 -9 8 0x12 0
TX 10000 32
STOP
```

`SET` arguments are frequencyHz, bandwidthHz, spreading factor, coding-rate denominator (`5` means4/5), powerdBm, preamble length, sync word and inversion flag. Supported configuration is470–510MHz, BW125/250/500kHz, SF7–12 and CR4/5–4/8. Register acceptance does not qualify matching or RF performance across the entire range.

The source identifies itself as `amc-pager-tx-0.3`. It adds `GFSK 478625000`:2400symbols/s, ±4.8kHz deviation, BT0.5 and nominal−9dBm. A separate `TX` is still required; airtime, cooldown and STOP protections apply. Host example:

```powershell
python tools/pager_tx.py --port COM11 send-gfsk --frequency 478625000 --id 10001
```

`SET` returns to LoRa; reset defaults to LoRa idle. `AMP 478725000 OOK` and `AMP 478675000 ASK` generate finite24-segment carrier-level sequences. OOK alternates nominal−9dBm/off; ASK uses nominal−3/−9dBm. Hardware transitions occur between levels. The carrier stops and the previous configuration is restored afterward; no continuous-carrier command is provided. Host entry:

```powershell
python tools/pager_tx.py --port COM11 send-amp --mode OOK --frequency 478725000
```

Use `--mode ASK` for ASK. These controls test envelope classification, without simulating access-control or vehicle-key protocols.

Payload starts with `AMC1`, a little-endian32-bit test ID and deterministic pseudorandom bytes. The host records expected bytes and an FNV value. Expected transmitter bytes alone never prove received-byte equality; current LoRa PHY recovery must independently establish bytes and CRC from received I/Q.

The original report recorded a flashed `amc-pager-tx-0.2`; the source version does not establish what is currently flashed. `STATUS` exposes `started_count`, `completed_count`, latest start/completion ID, payload length/hash, estimated airtime and completion-interrupt duration. USB CDC buffering was enlarged, and completion is counted only after the TX_DONE interrupt. Duration includes startup overhead and is not precise measured airtime.

If a TX reply is lost, the host does not blindly retransmit: counts and IDs determine whether it already started/completed, with recovered receipts marked `receipt_source=status_recovery`. `SET` is idempotent and all parameters are checked after application. Exit paths attempt STOP and close the serial port.

## Build and operation

```powershell
pio run --project-dir firmware/pager_tx
pio run --project-dir firmware/pager_tx --target upload --upload-port COM11
python tools/pager_tx.py --port COM11 status
python tools/pager_tx.py --port COM11 send --frequency 478321000 --bw 250000 --sf 11 --power -9 --length 32 --id 10000
python tools/pager_tx.py --port COM11 stop
```

`tools/firmware_job.py` runs automated jobs in a hidden background process and saves logs and explicit exit codes under ignored `build/`. An observation timeout is not a build result.

## Original firmware backup

The private16MB flash backup is `firmware/backups/pager-com11-before-amc.bin`, with SHA-256/full-flash MD5 manifests. It contains device data and is ignored by Git; do not distribute it. Historical restoration command:

```powershell
python -m esptool --chip esp32s3 --port COM11 --baud 460800 write-flash 0 firmware/backups/pager-com11-before-amc.bin
```

Restoration overwrites the entire flash. The original task flashed test firmware and did not restore the backup. This documentation maintenance does neither.

## Reception association boundary

The receiver searches a fixed wide band; transmitter settings are not passed into the detector. After transmission, the host associates new recordings by time, frequency, bandwidth, spreading factor and polarity. Other transmitters can make that association ambiguous. Without independently recovered payload and CRC, call it a time/frequency-parameter association, without transmitter identity. Other captures remain unknown-source observations.
