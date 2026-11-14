# v3 周期 FSK 方向变化修复候选

[English](README.md) · 简体中文

这是独立于 v4 弱信号实验的 v3.1 候选补丁，尚未部署。

部署 v3 在独立合成评估的两个周期 FSK 输入中给出了 LoRa 结构事件。本补丁要求 SFD 下 Chirp 区间的匹配强度，相对前导码期间的同类反向投影至少增强 5 dB，再执行原有转换验证。它不改变前导码、同步、四分之一窗口、候选激活或结束位置优势门限。

`fix.patch` 修改 `src/lora.cpp`、`src/monitor.cpp` 和 `src/gui.cpp`。后者仅更新算法标记为 `dechirp-fft-structure-v3.1`。补丁适用于这里保存的 v3 基线，不能直接套用当前主源码中的 v4 实验实现。`baseline-sha256.json` 绑定原文件字节；三个 `.v3-base` 文件保留对应源码，`lora_test.py.v3-base` 保留原 LoRa 回归脚本。

## 已完成验证

- `git apply --check --ignore-space-change --directory=build/baseline-v3-source patches/v3-periodic-fsk/fix.patch` 通过。
- 本机隔离构建成功，原 v3 LoRa 回归通过。
- 相同生成器和逐样本 SHA256 的 240 个合成输入对照：三组正例检出数保持 29/30、4/30、0/30；周期 FSK 结构误判从 2/30 降到 0/30，其他四组反例仍为零。
- 结果见 `build/rf-tests/v3-fsk-fix-comparison.json`。这不是射频灵敏度或现场误报率证明。

## 复现方式

在独立构建源码目录复制项目的 CMakeLists.txt、include、src、tools 和 tests。将这里的 `lora.cpp.v3-base`、`monitor.cpp.v3-base` 分别恢复为该目录下的 `src/lora.cpp`、`src/monitor.cpp`，并将 `lora_test.py.v3-base` 恢复为 `tests/lora_test.py`。核对三个源码文件的 SHA256 后，在项目根目录通过 `git apply --ignore-space-change --directory=<隔离目录> patches/v3-periodic-fsk/fix.patch` 应用补丁，然后单独配置、构建和运行测试。

本次本机源码目录为 `build/v3-fsk-fix-source`，构建目录为 `build/v3-fsk-fix-build`。ARM 验证目录为 `/home/vicliu/signalscope-fsk-fix-v31`，构建与测试使用 Linux idle 调度优先级，不能把它的运行结果冒充已安装版本。

## 尚待验证

ARM 原生回归、固定 FSK 反例、真实实发和运行负载仍需完成。线上 v3 的 24 小时观察保持不变，不能在尚未检查结果前声明修复已安装或验收完成。

## 长稳失败后补充的界面优化

原安装版在约第 33 分钟出现分析丢弃，观察器已失败退出，不能再称 24 小时验证正在通过。独立 ARM 构建已主动停止以保存现场。界面代码还发现每秒十次重复重建事件表，而状态约每秒更新一次；补丁现增加状态/频谱序号去重，并复用表格项、合并刷新。这部分尚未在 ARM 构建或实机验证，不能沿用此前纯方向变化修复的性能结论。

复现时还需将 gui.cpp.v3-base 恢复为隔离目录的 src/gui.cpp。补丁以 LF 保存，基线文件有混合行尾，应用时使用上面的 --ignore-space-change；三个源码基线字节 SHA256 均已核对。

后续部署可通过 tools/deploy_receiver.py 指定独立 --build-dir 和 --expected-algorithm；自定义构建必须先提供 --validation-report 与 --validation-note，核对同一二进制至少 300 秒实机运行及软件计数。当前未执行安装，不能使用历史 v3 报告为不同 SHA256 的候选背书。

部署门禁最终设为所有部署均必填验证报告及说明，移除了沿用历史静态验证说明的默认路径；门禁测试和 --help 检查通过，未执行安装。

## ARM 阶段性结果更新

两个固定 FSK 反例已在 ARM v3.1 CLI 上通过，实际输入样本数、算法标记与 SHA256 均匹配；结果为 build/rf-tests/v31-fsk-regressions-arm.json。相同二进制的 300 秒真实采集测试中，采集/分析/录制处理均为 525000000 对，软件队列丢弃零、结束积压零。五种重点配置各两次和六种补测共 16 次 −9 dBm 实发均成功关联并保存预计整帧 I/Q；未验证载荷或 CRC。报告 v31-live300.json、matrix-v31-fskfix-focused-repeat2.json、matrix-v31-fskfix-controls.json。

GUI 也已成功链接并完成离屏短录制重放、截图及原始数据保护检查。各十秒静态刷新测量窗口中，旧版主线程约 0.70 秒 CPU，新版约 0.16 秒；两次均处于同一 10% 单核配额环境，不能外推实时整机 CPU 或证明无丢样。图像 v31-gui-replay-offscreen.png 已人工检查。完整 ARM 原生测试仍在整体构建之后等待执行；尚未安装新 GUI，实时界面负载和新的长稳仍待验证。
