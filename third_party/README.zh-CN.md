# 原生电视依赖

[English](README.md) · 简体中文

原生电视接收链和桌面播放器已经实现。验收区分波形、传输流、编解码、界面播放与平台证据；原生解码测试通过不能单独证明天线实收。参见[电视规格](../docs/20-DTMB节目流与电视播放规格.md)及对应日期的验收报告。

| 依赖 | 固定源码 | 许可及用途 |
|---|---|---|
| dtmb-sdr | `ningzichun/dtmb-sdr`，`2cc94c012128d6fa3e8d6de456b3bb57e51990a9` | MIT，原生私有接收辅助进程与 LDPC 表，不依赖 Python 运行时 |
| FFmpeg | `FFmpeg/FFmpeg`，`cd4e96eb314630788b7ff2f9820c395788c023e2` | 所选共享库构建适用 LGPL2.1或以后版本；禁用 GPL/nonfree；包含原生 AVS1-P16 Broadcasting 解码 |
| DRA 核心 | `maliwen2015/ffmpeg_cavs_dra`，`eb94ceb887b47b6ecfbda9da43273dd65246df1e` | MIT，仅提取解码 C 源码；来源和本地修正见 [dra/UPSTREAM.zh-CN.md](dra/UPSTREAM.zh-CN.md) |

dtmb-sdr 源码归档 SHA-256：`87ecbf94ae4873bcc65c3a284ab4e2bfeecf26f2f8135a2f0ff2d98af8338c41`。

[FFmpeg 固定源码归档](https://codeload.github.com/FFmpeg/FFmpeg/tar.gz/cd4e96eb314630788b7ff2f9820c395788c023e2) SHA-256：`e3976ef0636e9c12d68c193abc4898ab50f05be3a4996026abc60c18be7447d4`。

FFmpeg9.0.2 仅接受 JiZhun 配置，不能满足 AVS+ 验收。不能因为系统存在名为 `cavs` 的解码器就替换固定源码；官方 AVS+ FATE 输入必须匹配官方逐帧校验值。DRA 还须使用真实编码输入和 PCM 验证；合成编解码提供者只验证扩展机制。

公开参考媒体和广播录制属于验证输入，不是可再分发的应用素材，保留在跟踪源码和发布包之外。发布须包含适用许可证及源码/重新链接信息，在每个平台构建同一固定实现，并验证实际安装的二进制。

桌面默认从固定源码组装共享 FFmpeg SDK。`SIGNALSCOPE_FFMPEG_ROOT` 是显式开发覆盖；`SIGNALSCOPE_TELEVISION=OFF` 可构建有意不包含电视的版本。Linux/macOS 需要 shell、GNU Make，x86 还需 NASM；Windows x64 使用 MSVC、MSYS2 shell、Make/NASM。正式发布工作流提供这些工具。安装包含共享运行库、许可、完整 FFmpeg 源码归档和构建配方。添加构建规则并不等于已通过该平台实际归档验收。

Windows 配方接受普通及全大写 MSVC 环境键。针对只有本地化诊断的编译器，在构建树复制固定 FFmpeg configure 脚本，移除微软标记检测中的四个行锚点；严格验证原上下文，下载源码和打包源码归档保持不变。随包的 `BuildFfmpeg.sh` 包含完整转换与调用。资源包装脚本也在构建树复制，向 rc.exe 传递 UTF-8 代码页65001，使本地化编译器元数据保持有效。修改只影响编译器识别，不改变编解码算法或能力检查。

`src/adapters/television/dtmb/symbol_sampler.hpp` 的有界多相 SRRC 滤波器组源自固定 dtmb-sdr 的 `core/cpp/src/iq.cpp`，原 MIT 声明保存在 `dtmb-sampler-LICENSE`。扩展提供分数判决相位、绝对有限探测起点、增量时间累加、毫赫兹时钟修正和持久有界工作组。私有 CF32 采样器协议为 v3；接收适配器检查版本，并在探测/流式进程使用同一实测时序与时钟。时钟捕获使用超帧相关，含 CW/噪声反例。仅有这些实现不能证明长期时钟跟踪或实收质量，仍以实际验收证据为准。

固定依赖的 PN595 与有限 FEC 尾部修正见 [dtmb-patches/README.zh-CN.md](dtmb-patches/README.zh-CN.md)，准确补丁清单随依赖声明一同分发。
