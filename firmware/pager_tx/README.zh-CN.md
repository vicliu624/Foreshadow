# Pager 串口可控低功率 LoRa 与 GFSK 测试固件

[English](README.md) · 简体中文

面向用户指定 COM11 的 LilyGO T-LoRa-Pager SX1262 版本。仅用于本项目自发射、自接收调试；不依赖 Trail Mate 业务代码。使用 Arduino ESP32 和 RadioLib 7.4.0 驱动。无屏幕界面，不初始化 Wi-Fi、蓝牙或 GNSS。

## 行为与约束

- 上电保持待机，不自动发射、不自动循环，不从 NVS 恢复发射任务。
- 默认名义功率 −9 dBm，串口仅允许 −9～0 dBm。该数值是芯片设置，不是实测辐射功率。
- 每条 TX 命令仅发一包，载荷 8～240 字节。单包预计空中时长不超过 1 秒，超限请求在发射前拒绝。
- 开始时刻之间至少间隔 2 秒，且至少为预计空中时长的 10 倍。
- 异步发射；STOP 可中断当前包。看门狗处理未收到完成中断的情况。
- 使用显式头和 2 字节 CRC；固件输出 TX_DONE 只代表发射芯片状态，不代表接收成功。

## 硬件连接

SPI：SCK35、MISO33、MOSI34；SX1262：CS36、DIO1/IRQ14、RESET47、BUSY48。共享 SPI 的显示、SD、NFC CS 均先置高。I2C：SDA3/SCL2；XL9555 地址 0x20，仅修改 port0 bit3 的 LoRa 电源位。TCXO 配置采用 1.6 V。板型和实际无线电初始化已通过串口回读验证。

## 串口协议

115200，ASCII，一行一条命令，以换行结束；回复为 JSON 行。参数不持久化，复位回到默认待机配置。

```text
PING
STATUS
SET 478321000 250000 11 5 -9 8 0x12 0
TX 10000 32
STOP
```

SET 参数依次为：频率 Hz、带宽 Hz、SF、编码率分母（5 表示 4/5）、功率 dBm、前导码长度、同步字、反相标志。当前支持 470–510 MHz 配置，BW125/250/500 kHz、SF7–12、CR4/5～4/8。寄存器接受配置不等于验证了所有频率的板级匹配性能。

固件 `amc-pager-tx-0.3` 增加 `GFSK 478625000`，配置 2400 symbol/s、±4.8 kHz 频偏、BT 0.5 和名义 −9 dBm。发送仍由单独的 TX 命令触发，使用相同的时长、冷却和 STOP 保护。主机可用 `python tools/pager_tx.py --port COM11 send-gfsk --frequency 478625000 --id 10001`。发送 LoRa 时，`SET` 会切回 LoRa，复位默认仍为 LoRa 待机。

实验命令 `AMP 478725000 OOK` 和 `AMP 478675000 ASK` 生成有限的载波电平序列。OOK 使用名义 −9 dBm 和关断，ASK 使用名义 −3／−9 dBm，射频状态切换之间存在硬件过渡间隔。每个命令固定 24 段，结束后关闭载波并恢复原配置，不提供连续载波命令。主机入口为 `python tools/pager_tx.py --port COM11 send-amp --mode OOK --frequency 478725000`，或使用 `--mode ASK`。这些波形用于验证包络分类，不模拟门禁或车钥匙协议。

载荷前 4 字节为 AMC1，随后为小端 32 位测试 ID，其余为确定性伪随机字节。主机保存预期载荷和 FNV 校验值，用于对照独立解调结果；当前 LoRa PHY 恢复仍须从接收 I/Q 独立得到字节和 CRC，不能仅凭发射端预期值声称收到相同字节。

原实测记录的烧录版本为 `amc-pager-tx-0.2`；当前源码标识为 `amc-pager-tx-0.3`，不代表已经重新烧录设备。STATUS 还提供 `started_count`、`completed_count`、最近开始/完成的 ID、载荷长度和校验值、预计空中时长及完成中断耗时。固件扩大 USB CDC 缓冲，并在确认 TX_DONE IRQ 后才增加完成计数。完成耗时包含启动调用等开销，不能直接当作精确空中时长。

主机不会在 TX 回复丢失时盲目重发同一命令；它通过状态计数及 ID 核对是否已经开始或完成，并在恢复的收据中标明 `receipt_source=status_recovery`。SET 是幂等配置，主机会核对全部参数回读。所有退出路径尽力发送 STOP 并关闭串口。

## 构建与操作

```powershell
pio run --project-dir firmware/pager_tx
pio run --project-dir firmware/pager_tx --target upload --upload-port COM11
python tools/pager_tx.py --port COM11 status
python tools/pager_tx.py --port COM11 send --frequency 478321000 --bw 250000 --sf 11 --power -9 --length 32 --id 10000
python tools/pager_tx.py --port COM11 stop
```

自动任务通过 tools/firmware_job.py 在隐藏后台运行，日志和明确退出码保存在 build/ 中，不把观察超时当作构建结果。

## 原固件备份与恢复

原始 16 MB Flash 备份位于 firmware/backups/pager-com11-before-amc.bin，旁有 SHA256/整片 MD5 清单。备份含原设备数据，整个目录已被 Git 忽略，不应发布。

```powershell
python -m esptool --chip esp32s3 --port COM11 --baud 460800 write-flash 0 firmware/backups/pager-com11-before-amc.bin
```

恢复会将整片 Flash 写回备份状态；当前任务只执行了测试固件烧录，没有执行恢复。

## 接收关联的边界

接收器保持固定宽带自动搜索，发射参数不传入检测器。主机只在发射后按新录制、时间、频率、BW/SF 和极性进行关联；户外同频环境信号可能造成歧义。没有载荷解调与 CRC 验证时，只能称为时频参数关联，不能证明发射端身份。其他捕获继续标为来源未知。
