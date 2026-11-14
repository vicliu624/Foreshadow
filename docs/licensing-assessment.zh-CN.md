# 许可证评估与分发缺口

[English](licensing-assessment.md) · 简体中文

**当前更新，2026-11-14：**下文评估的进程内 SDRplay 方案已经替代。独立的 0.2.12 基础包与网络服务客户端各自通过真实归档／材料门禁，均不包含或加载厂商 API／驱动。见[服务指南](receiver-services.zh-CN.md)及 [r4 验收](reports/2026-11-14-receiver-service-review.zh-CN.md)。下文 11 月 13 日清单与直连疑问保留为有日期证据；厂商 API 再分发和固件清权仍单独处理。

[免费开源编解码评估](codec-patent-assessment.zh-CN.md)现按实际解码器集合核对一手许可资料。`0c5fb5e` 新包已修正构建示例并通过复验；[分别准备的发布资产](reports/2026-11-14-public-release-preparation.zh-CN.md)明确完整基础包的未决专利判断与客户端资格，不推定免费下载自动豁免或必需购买许可。

评估日期：2026-11-13。这是工程评估，不是授权文件，也不是已经完成的法律清权结论。用户首先关心满足上游要求，避免不兼容分发及由此产生的通知。用户现已授权主程序与自研插件采用 GPL-3.0-or-later，[许可范围](../LICENSING.zh-CN.md)明确实际授权及排除项；PCM 改造与主许可分别记录。

## 已采纳的许可方向

对于当前功能完整、开放源码的桌面应用，**第一方应用代码采用 GPL-3.0-or-later** 是已采纳的方向。Linux 主体确实链接 GPL-2.0-or-later 的 FFTW；RTL-SDR 模块链接 GPL-2.0-or-later 的 librtlsdr。组合分发选择 GPLv3，也能容纳当前 Apache-2.0 依赖与 LGPLv3 Qt。用户现已明确授权该第一方范围；这不重新授权第三方代码，不解决厂商 API 的权利，也不自动完成发布义务。

MIT/Apache-2.0 可以授权我们独立拥有的源文件，但在根目录写宽松许可，不能让结合 FFTW/librtlsdr 的二进制也变成宽松许可。改用 `dlopen`、把模块单独打包，都不能单独解决组合程序的问题。FSF 在 [GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#GPLPlugins) 解释了静态/动态链接及插件；这是许可方的解释，不是对所有架构的统一司法裁定。Apache 也明确说明了[与 GPLv3 的兼容性](https://www.apache.org/foundation/license-faq.html#GPL)。

**仅采用 GPLv3，仍不能直接认定 SDRplay 链路已获许可。** 实测 Linux 链路是主体 → 原生接收机模块 → SoapySDR → SoapySDRPlay3 → `libsdrplay_api.so.3.15`，在同一进程内运行。前两层开源适配的许可不授予厂商 API 的权利。正式清权前需要取得准确版本条款，确认链接与组合分发许可；让用户另装 API，也不会自动解决链接兼容性。我们只能为自己拥有的代码授予例外，不能替 FFTW/librtlsdr 放弃条件。真正独立的接收服务及适当协议，或移除/替换相关 GPL 依赖，是可以评估的工程路径，各自仍需复核。

## 证据和范围

已检查 CMake 链接与安装规则、正式发布/工具箱组包、固定源码、仓库内许可、Windows vcpkg 记录及链接命令、Qt 6.8.3 SPDX SBOM、已安装 Windows 评审目录，以及接收机所在 Debian 13 工作站的 `ldd` 和 Debian copyright 文件。基线包源码为 `d7cfd271c1ee19e022051676fac48c545dab7381`；初始 PCM 改造提交为 `7ba437a`；设备格式转换和输出测试发现修正为 `e44c8a7dd9a6ae41a5058b1640b4326dc3ba9650`。[清单证据](reports/2026-11-13-license-inventory.json)保留观察到的 SDK 元数据及二进制许可返回值，最终 PCM/运行时源码为 `a258670d92525c43e5dd6cf104b9965108c9198c`，见[验收与本地部署](reports/2026-11-13-pcm-and-licensing-review.zh-CN.md)。

本清单确认直接依赖及重要的传递依赖组，**还不是逐文件、逐发布包的完整 SBOM**。Qt 模块 SBOM 同时包含工具、未用模块和运行时，照抄整个 SDK 清单会误称所有内容均随包分发。最终产物必须分别生成实际依赖闭包、许可选项和源码交付材料。本轮没有重新验收 macOS 原生运行时闭包与安装。

## 应用与接收机依赖

| 组件 | 已观察版本与用途 | 上游许可 | 分发要求 |
|---|---|---|---|
| nlohmann/json | 3.12.0，哈希固定，头文件编入程序 | [MIT](https://github.com/nlohmann/json/blob/v3.12.0/LICENSE.MIT) | 源码与二进制配套材料保留版权及许可声明。 |
| KissFFT | 131.1.0，哈希固定，静态 FFT 后备实现 | [BSD-3-Clause](https://github.com/mborgerding/kissfft/blob/131.1.0/COPYING) | 保留实际版权声明和 BSD 条款，不能只有通用许可模板。 |
| FFTW | Linux 3.3.10，主体 DSP 与 DTMB 辅助进程链接；所查 Windows 未启用 | [GPL-2.0-or-later](https://fftw.org/doc/License-and-Copyright.html) | 组合分发满足 GPL，交付对应源码及构建信息；商业例外需要真实授权。动态链接不豁免。 |
| VOLK | Linux 3.2.0，可选向量 DSP | [LGPL-3.0-or-later](https://github.com/gnuradio/volk/blob/v3.2.0/kernels/volk/volk_32fc_magnitude_32f.h) | 保留条款、声明、对应库源码和适用的替换权。不能把旧 2.x 的 GPL 结论套在实测 3.2.0 上。 |
| librtlsdr | Windows/Linux 2.0.2，RTL 原生模块；Windows 静态链接 | [GPL-2.0-or-later](https://github.com/osmocom/rtl-sdr/blob/v2.0.2/src/librtlsdr.c) | 提供模块完整链接作品的源码、构建和补丁信息；独立工具箱也有义务。 |
| libusb | Windows 1.0.30 静态进入 RTL 模块；Linux 1.0.28 系统库 | [LGPL-2.1-or-later](https://github.com/libusb/libusb/blob/v1.0.30/libusb/core.c) | 明确选择允许的履约方式。静态编入要有可重链接/对应源码方案，不能只放 COPYING。 |
| Pthreads4w | Windows 3.0.0，RTL 模块静态链接 `pthreadVC3.lib` | 所查 vcpkg copyright 为 Apache-2.0 | 保留许可、适用通知及准确源码/补丁来源。 |
| SoapySDR | 0.8.1，Windows 动态 DLL、Linux 系统库 | [BSL-1.0](https://github.com/pothosware/SoapySDR/blob/soapy-sdr-0.8.1/LICENSE_1_0.txt) | 源码/材料保留来源和声明；每个驱动另查。 |
| SoapySDRPlay3 | 工作站已安装的原生驱动 | [MIT](https://github.com/pothosware/SoapySDRPlay3/blob/master/SoapySDRPlay.hpp) | 下文记录缓存源码修订与安装前后匹配的 ELF build ID。MIT 不覆盖下层厂商 API。 |
| SDRplay API | 工作站解析到 `libsdrplay_api.so.3.15`；不在所查应用归档中 | [厂商 API 条款](https://sdrplay.com/hardware-api/)，准确授权待核实 | 取得并保留对应版本 EULA，确认再分发/链接权限；免费下载不等于开源或允许再分发。 |
| OpenSSL | Windows 3.6.5 静态 Crypto；Linux 3.5.7 系统库 | [3.x 为 Apache-2.0](https://openssl-library.org/source/license/index.html) | 保留条款、声明和准确源码身份。CMake 已要求 3.0+，评审配置绑定实际版本，不把 3.x 结论套用给条款不同的 1.1.1。 |
| libarchive | Windows 3.8.9 静态；Linux 3.7.4 系统库 | [BSD 风格逐文件条款](https://github.com/libarchive/libarchive/blob/v3.8.9/COPYING)，以 BSD-2-Clause 为主 | 保留原 COPYING 和逐文件条款，并盘点压缩/密码学传递依赖。 |

Windows libarchive 依赖记录包含 zlib 1.3.2（Zlib）、bzip2 1.0.8（bzip2 条款）、liblzma 5.8.4（库为 0BSD）、LZ4 1.11.1（库为 BSD-2-Clause）、Zstandard 1.5.7（库可选 BSD-3-Clause 或 GPL-2.0-only，本方案选择 BSD）。准确 copyright 文件在构建缓存中，但不在所查安装目录的许可树中。libarchive 自身也有逐文件公共领域和宽松许可声明，不能只用一行“BSD”代替。Debian 系统传递依赖还包括 libxml2、nettle、ACL 等，没有拷入当前应用归档；未来制作包含这些库的便携包时，需要重新盘点运行时和源码。

## Qt：使用范围与需要的证据

Windows 为 Qt **6.8.3**，工作站为 Debian Qt **6.8.2**。所查 Windows SBOM 为下述运行时模块提供 LGPL-3.0-only 选项，同时列有其他 GPL/商业选项。满足 LGPL 条件即可选择开源路线，商业使用本身不意味着必须购买 Qt 商业许可。参见 [Qt 6.8 许可](https://doc.qt.io/qt-6.8/licensing.html) 和 [Qt 官方 LGPL 义务说明](https://www.qt.io/development/open-source-lgpl-obligations)。

| Qt 组件 | 实际用途 | 范围决策 |
|---|---|---|
| Core / Gui / Widgets | 桌面、资源、控件、绘制 | 保留动态库；`src/backend` 及其公共后端头文件没有直接引入 Qt。 |
| Network | 桌面网络传输适配 | 保留在平台/网络适配边界。 |
| Multimedia | 原生 PCM 输出设备 | 保留 QAudioSink/QMediaDevices；研究 WAV 回放不再需要 QMediaPlayer。 |
| SerialPort | Pager 串口工具 | 为该可执行文件保留，并覆盖其依赖许可。 |
| Svg、平台/图片/图标/TLS/样式插件 | Windows 自动部署实际带入 | 按实发内容归属和声明；删减前检查依赖和行为。 |
| Qt 翻译 | Qt 控件本地化，当前 6.8.3 SPDX 可选 BSD-3-Clause | 保留准确声明；英文界面不等于没有分发语言资源。 |
| LinguistTools、moc、rcc、部署工具 | 构建工具 | 工具许可/Qt 例外与运行时分别记录；用了工具不等于应用链接 GPL-only 的 Qt 模块。 |
| WebEngine、Charts、Quick/QML、Graphs、Virtual Keyboard | 所查 Windows 运行时没有这些模块 | SDK 中存在不代表可直接新增；引入前按具体模块、具体版本审核。 |

Qt 正式发布需要提供的证据：

1. 固定补丁版本、二进制来源、模块/插件闭包和所选开源许可。滚动 `brew install qt` 或未固定的软件包更新不是永久许可记录。
2. 随包交付适用 LGPL/GPL 原文、显著 Qt 使用声明及版权信息，保留内部第三方声明和适用源码要求，不能只有 Qt 网站链接。
3. 通过许可允许的方式提供准确 Qt 对应源码、修改和构建信息。优先与二进制发布一起提供不可变源码附件，避免没有材料支持的“找上游要”。
4. 保持动态库可替换，给出构建/替换说明；适用时提供安装信息。macOS 签名及用户自行重签需要原生验证，不能只因 DLL 是动态库就宣布通过。
5. EULA 不得禁止为行使 LGPL 权利所需的修改/逆向工程。官方目录的信任控制不能演变为禁止收件人在本地副本行使相应权利。
6. 发布检查阻止条款未知或不兼容的新模块。购买 Qt 商业许可也不会豁免 FFmpeg 等独立组件的条件。

Qt 内部第三方组件是另一层。所查 SDK SBOM 包括 PCRE2/sljit、zlib、libpng、libjpeg-turbo、FreeType、HarfBuzz、Unicode/CLDR、double-conversion、TinyCBOR、md4c、公共后缀数据及若干小组件声明。涉及 BSD/MIT、Zlib、libpng/IJG、FTL、Unicode-3.0、MPL-2.0 和特定许可引用。GPLv3 兼容组合应明确选择 FreeType 的 **FTL** 路线，不能以为它的 GPL-2.0-only 备选项可以随意替换。按实发闭包筛选 SDK SBOM，提供适用原始声明与源码；不能把所有 SDK 包或 NOASSERTION 都写成已核实的运行时许可。

原先评审包有 Qt DLL，但缺少完整 Qt 声明和源码材料；本轮已新增独立材料协议，最终验收按实际归档分别记录。**功能验收通过不等于许可证验收通过。** 无法保证上游永远不发送通知，实际目标是对每项条件留下可复核的履约证据。

## 电视编解码与双 FFmpeg 整改

| 组件 | 固定源码 | 许可与处理 |
|---|---|---|
| dtmb-sdr | `2cc94c012128d6fa3e8d6de456b3bb57e51990a9` | [MIT](https://github.com/ningzichun/dtmb-sdr/blob/2cc94c012128d6fa3e8d6de456b3bb57e51990a9/LICENSE)；保留许可、采样器来源和本地补丁记录。链接 FFTW 的辅助进程还要满足相应 GPL 组合分发条件。 |
| 原生 FFmpeg | `cd4e96eb314630788b7ff2f9820c395788c023e2` | [当前选定构建为 LGPL-2.1-or-later](https://ffmpeg.org/legal.html)，动态库、禁用 GPL/nonfree；电视声明目录已有准确完整源码归档和构建配方，仍需复查显著声明与替换说明。 |
| DRA 核心 | 从 `eb94ceb887b47b6ecfbda9da43273dd65246df1e` 提取 | 上游仓库的 [MIT 声明](https://github.com/maliwen2015/ffmpeg_cavs_dra/blob/eb94ceb887b47b6ecfbda9da43273dd65246df1e/LICENSE)；文件、哈希和本地修改见 [UPSTREAM](../third_party/dra/UPSTREAM.zh-CN.md)。版权许可不等于专利清权。 |

旧 Windows 评审包同时有 Qt 带入的 `avcodec-61.dll` 和原生电视 SDK 的 `avcodec-63.dll`。分别调用 DLL 自身的 `avcodec_license()`，两者均返回 LGPL version 2.1 or later，但仍是两个独立源码/构建身份；所查包只有原生 SDK 的源码归档。第二套也不只是一个无用文件：通用分析面板曾用 QMediaPlayer 播放提供者返回的 WAV。

用户已授权将这条路径改为校验后的有限 PCM 输出。`ResearchAudio` 协议保持标准 16 位 PCM WAV、单/双声道、8–192 kHz；研究模块负责校验及 QAudioSink 生命周期；必要时以每次最多 64 KiB 的有限源转换采样率、声道和样本类型，适配设备推荐格式，不构造整份重采样输出。转换只用于音频呈现，不改变恢复样本或分析证据。主体继续消费通用音频结果，导出仍保留原始 WAV。FM 和电视此前已使用 PCM 输出，没有引入提供者 ID 分支或新的样本编解码假设。

进程映射探测证明，仅替换 QMediaPlayer 还不够：Debian 的 Qt 在音频设备初始化时仍会选择已安装的 FFmpeg 插件。音频所属模块现在明确初始化内置的 PCM 回退实现，随后立即恢复环境变量，让独立启动的研究工具保留自己的后端设置。依据是已检查的 [Qt 6.8.2 实现](https://github.com/qt/qtmultimedia/blob/v6.8.2/src/multimedia/platform/qplatformmediaintegration.cpp)和 [Qt 6.8.3 实现](https://github.com/qt/qtmultimedia/blob/v6.8.3/src/multimedia/platform/qplatformmediaintegration.cpp)。加载映像测试会拒绝 PCM 进程引入编解码后端，升级必须通过这一行为契约；这不是对所有未来 Qt 版本的保证。

Qt 6.8+ 的 Windows 部署跳过媒体编解码插件及 Qt 的 FFmpeg。macOS 安装组装在 Mach-O 重定位前移除媒体编解码插件/其他 FFmpeg ABI，恢复原生 SDK；独立假包测试验证保留规则和幂等性，不代表 Mac 原生验收。Linux 归档原本使用系统 Qt、只携带原生 SDK，系统安装内容另行核对。更低 Qt 版本不在新部署筛选保证范围内。实际归档检查要求每个 FFmpeg 库只有一个真实运行时，并用解压后的库验证 PCM 输入与输出；没有音频设备明确跳过，不能写成播放成功。

编解码版权许可与专利权是两件事，本评估不为所有地区/商业分发清除 AVS、DRA、H.264、H.265、AAC 专利要求。外部实录参考媒体仍是验证输入，不放入源码或发布包。

## Pager 固件与历史源码副本

仓库还包含 `firmware/pager_tx`，它是独立的嵌入式产物，不是桌面运行时依赖。PlatformIO 配置固定 `espressif32@6.10.0`、RadioLib 7.4.0，采用 Arduino 框架。

| 组件 | 许可与证据 | 处理要求 |
|---|---|---|
| RadioLib 7.4.0 | [固定版本上游清单声明 MIT](https://github.com/jgromes/RadioLib/blob/7.4.0/library.json) | 分发固件源码/二进制材料时保留实际版权与许可文件。 |
| PlatformIO espressif32 6.10.0 | [平台清单为 Apache-2.0](https://github.com/platformio/platform-espressif32/blob/v6.10.0/platform.json)，Arduino 包范围为 `~3.20017.0` | 记录实际解析的框架、工具链版本和哈希；固定平台版本不等于完整固件 SBOM。 |
| Arduino-ESP32 核心 | [LGPL-2.1 原文](https://github.com/espressif/arduino-esp32/blob/2.0.17/LICENSE.md)，可用缓存 `3.20017.241212+sha.dcc1105b` 声明 LGPL-2.1-or-later | 静态固件分发要提供适用库源码/修改及可重链接方案，包括足够的应用对象文件或可复现源码/构建材料。 |
| ESP-IDF/嵌入式 SDK 与运行时 | [原生代码 Apache-2.0，其他组件分别许可](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32s3/COPYRIGHT.html)，含 MIT/BSD 和另有条款的组件 | 按最终 ELF/链接映射和准确框架包确定实际组件。不能把所有 SDK 归档标成 Apache-2.0，也不能仅因某无线功能没用到就认定其二进制一定被链接或一定被排除。 |

原盘点没有解析实际固件构建；现已补充构建、初始归档列表与链接器回收后的有效段证据，见下文。固件许可仍未清权。桌面 GPL 建议要明确适用范围；固件另列经审核的范围。独立拥有的固件代码可考虑宽松许可，但不能免除 Arduino LGPL 或 SDK 条件。

`research/v4/source-before-buffering.tar` 有 84 项历史源码条目，没有许可证命名条目；`patches/v3-periodic-fsk` 也保留旧源码。保持这些证据身份，最终许可范围和清单要覆盖这些第一方副本及导入内容；新增根许可证不能证明嵌套历史产物都已有完整声明。

## 工具、测试数据与文档

Python 是维护/测试工具，不是应用必需运行时。可选 NumPy/SciPy（BSD）、Matplotlib（基于 PSF 的许可）、pySerial（BSD-3-Clause）、esptool（GPL-2.0-or-later），在分发相应工具/包时需要记录。开发机安装这些软件，不会自动重新授权 DSP 代码。仓库的 `extract_tables.py` 会导入 **PyMuPDF**，其提供 [AGPL/商业许可](https://pymupdf.io/licensing)。要把提取器作为独立链接工具审核：替换依赖，或明确满足其条件；既不能推断原生应用因此变成 AGPL，也不能因为它不在桌面归档里就完全忽略。

CMake、Ninja、NASM、Make、编译器、MSYS2 是构建工具。Windows 包还包含 MSVC 运行时、D3DCompiler、软件 OpenGL；要保留真实的微软再分发授权/来源及软件渲染器的第三方声明。Linux 系统 C/C++ 运行时和 macOS LLVM 运行时各有许可/例外，拷入包的部分都要登记。使用 GPL 构建工具本身不能证明应用链接了 GPL 库。

合成电视媒体样本及来源记录已有 CC0-1.0，保持其范围，不能当成应用许可证。RF 样本、公开互操作向量、标准提取数值表和旧报告图片分别记录来源/权利；可访问不等于广播录音或标准 PDF 可再分发。用户录制与导出分析数据不会仅因软件生成/读取就自动变成 GPL。第一方文档/截图可由权利人另外选择 CC-BY-4.0，导入的声明/数据仍保留各自条款。

## 已实现的授权与发布协议

根目录保留未经修改的 GNU GPLv3 原文，[许可范围](../LICENSING.zh-CN.md)明确第一方范围和排除项。[第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)与[源码／替换说明](../SOURCE_AND_REPLACEMENT.zh-CN.md)均有中英文版本。上文盘点描述此前发布包基线，不能代替新包实际生成的清单。

提供者通过 schema 1 声明登记各包许可根；评审过的平台配置固定源码及 SHA-256、许可替代项、运行库归属，以及 Qt／OpenSSL／应用构建版本。通用收集器组装真实源码归档、原始声明与构建／补丁规则；每个独立接收机和研究包携带自身闭包，桌面 Help 菜单打开生成的材料索引。稳定的接收机、会话、界面和安装器没有按提供者 ID 增加许可行为分支。

组包拒绝缺失或变化的材料、未知运行库及源码／构建版本不符。私下评审选项保留可见的未决条件，不能绕过上述检查。正式签名重新打开归档，对照仓库评审过的平台配置复查身份、闭包、未决条件与运行库规则；删除生成清单中的问题不能获得发布资格。正式工作流以 HTTPS URL 和 SHA-256 固定经过评审的材料归档，未经评审的平台／版本没有自动放行。

SDRplay 的证据已比原盘点更直接：工作站缓存有 API 3.15 installer v2，其 AMD64 API 库 SHA-256 与已安装的 `/usr/local/lib/libsdrplay_api.so.3.15` 完全相同；随包 EULA 生效于 2021-01-12，与旧公开镜像哈希相同。因此已确认版本对应的实际条款，并非仅凭镜像推测。但条款仍含硬件用途和逆向限制，未建立再分发及 GPL 组合使用授权；包内不放厂商二进制，相关配置仍限评审。

缓存的 SoapySDRPlay3 源码提交为 `48bd8b41072534018de1d74deb3dea5874d9e0e0`。已安装驱动与缓存构建的 GNU build ID 均为 `7f707482ac60e32a070d4dae61a838de2ed996a7`；安装前后 ELF 字节哈希不同，证据保留该区别。交付 MIT 源码与声明作为支持材料，不将其解释为厂商 API 授权。

实际 pager_tx 构建现已解析平台 6.10.0、Arduino 包 `3.20017.241212+sha.dcc1105b` 和 RadioLib 7.4.0；初始归档列表包含无线库，但链接器会回收未用段。进一步检查正长度、非零已分配地址的最终输入段，确认 libcoexist.a 贡献 1,275 字节，以及 SDK 包装层和其他运行时；未发现 libnet80211.a、libpp.a、libphy.a、libbtbb.a 或 libbtdm_app.a 的有效段贡献，不能把初始归档清单当作最终 ELF 内容。当前上游共存库对目标形式声明 Apache-2.0，但精确 SDK 修订及源码义务仍需单独固件验收。[固件证据](reports/2026-11-13-firmware-license-evidence.json)记录版本、归档名与 ELF／map 哈希；没有烧录、没有发射。固件发布前仍需源码／重链接材料、SDK 逐组件条款及独立的第一方授权。

SDRplay 配置与固件的公开分发结论仍为 **未清权**。新评审产物分别记录声明／源码交付与原生功能验收，两者都不是普遍法律保证。macOS 不属于本次 Windows/Linux 验收。

## 2026-11-14 服务边界替代此前直连配置

上文 2026-11-13 的直连包结论保留为带日期证据。0.2.12 基础包排除可选 SDRplay 客户端，
客户端通过 BSL SoapyRemote 网络协议连接独立安装的服务，不携带或加载厂商 API、本地驱动。
因此基础包及客户端按各自实际依赖闭包检查，不继承原直连许可疑问。API EULA 和固件没有被重新
授权，也未取得新清权。SDRconnect 控制缺口、原生运行的外部阻断、后备选择与边界见
[服务评估](receiver-services.zh-CN.md)。
