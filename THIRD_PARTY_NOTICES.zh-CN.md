# 第三方许可声明

[English](THIRD_PARTY_NOTICES.md) · 简体中文

Foreshadow 主程序与自研插件采用 GPL-3.0-or-later。第三方组件保留原许可证。
本文件说明依赖类别；每个实际二进制包的
`share/foreshadow/compliance/inventory.json` 是带版本的交付清单，`index.html`
链接原始声明、源码归档与构建材料。桌面评审包可从 **Help → Licenses and source**
打开；独立下载的接收机和研究插件也携带各自适用的材料。

| 组件 | 选择的许可 | 使用与交付要求 |
|---|---|---|
| Qt 6 Core、Gui、Widgets、Network、Multimedia、SerialPort、Svg 及部署插件 | LGPL-3.0-only，以及模块内第三方的宽松许可 | 动态链接桌面库；交付对应模块源码、SDK/源码元数据、原始声明和替换说明。Qt 翻译保留 BSD-3-Clause 声明。源码和声明可能包含未运行的工具及代码，属于明确标注的源码超集，不代表这些二进制随包分发。 |
| Qt 内嵌第三方代码 | BSD、MIT、Apache、Zlib、Unicode、FreeType FTL 等原始条款 | 保留 qt_attribution.json、LICENSES 和原始版权文件；FreeType 选择 FTL，避免其 GPL-2.0-only 替代选项冲突。此清单不构成随意添加 Charts、Graphs、WebEngine 或 QML/Quick 功能的许可。 |
| nlohmann/json 3.12.0 | MIT | 头文件代码；保留版权与完整授权文本。 |
| KissFFT 131.1.0 | BSD-3-Clause | 静态 FFT 后备；保留 COPYING 中实际版权及许可证。 |
| FFTW 3.3.10 | GPL-2.0-or-later；组合交付选择 GPLv3 | Linux DSP 与 DTMB 辅助程序。 |
| VOLK 3.2.0 | LGPL-3.0-or-later | Linux 向量处理；保留 Debian 对应源码与补丁。 |
| RTL-SDR 2.0.2 | GPL-2.0-or-later；组合交付选择 GPLv3 | 接收机插件；本次 Windows 配置静态链接，Debian 使用系统动态库。 |
| libusb 1.0.x | LGPL-2.1-or-later | 接收机依赖；Windows 静态链接须交付源码及重新构建材料。 |
| Pthreads4w 3.0.0 | Apache-2.0 | Windows RTL-SDR 依赖，保留原始声明与源码。 |
| SoapySDR 0.8.1 | BSL-1.0 | 接收机抽象；开源 SoapySDR 不授予厂商 API 权利。 |
| SoapyRemote 0.5.4，修订 bd10a2e4 | BSL-1.0 | 可选网络接收机客户端；独立包交付固定原始源码和客户端构建配方，不含服务端及厂商 API 二进制。 |
| OpenSSL 3.x | Apache-2.0 | 目录签名验证；支持的构建最低版本提升为 3.0，不再用 3.x 声明覆盖 1.1.x。 |
| libarchive | BSD 风格的逐文件条款 | 包解压；保留完整原始源码声明。Windows 压缩依赖包括 zlib（Zlib）、bzip2（原始 bzip2 条款）、XZ/liblzma（0BSD 及逐文件条款）、LZ4（BSD-2-Clause）、Zstandard（选择 BSD-3-Clause）。 |
| FFmpeg cd4e96eb314630788b7ff2f9820c395788c023e2 | 当前库配置为 LGPL-2.1-or-later | 只保留一套原生动态解码库，配置 --disable-gpl --disable-nonfree；交付原始源码、cmake/BuildFfmpeg.sh、选项与校验后应用的移植补丁，运行时 -L/-buildconf 为补充证据。PCM 路径排除 Qt 编解码后端及其第二套 FFmpeg。 |
| dtmb-sdr 2cc94c012128d6fa3e8d6de456b3bb57e51990a9 | MIT | 保留源码、原始版权、PN595/FEC 补丁与采样器声明。 |
| DRA 提取分支 eb94ceb887b47b6ecfbda9da43273dd65246df1e | 导入代码为 MIT | third_party/dra 保留源码与修改记录；软件版权许可不等于编解码专利清权。 |
| Microsoft Visual C++ 运行时 | Microsoft 运行时／Visual Studio 再分发条款 | Windows CRT 原样分发，不改为 GPL。随 Windows 材料交付原始条款与官方再分发清单；区分 GPL 系统库排除与厂商再分发授权。 |

## 外部依赖与未决边界

**SDRplay：** 基础应用归档排除可选接收机客户端。独立客户端包含 BSL-1.0 的
SoapyRemote 0.5.4 客户端，修订为 `bd10a2e4a916bf66f0faa36dca7f043d4268138c`，
携带原始源码、声明和 `cmake/RemoteReceiver.cmake`。客户端连接独立 SoapySDRServer，
不加载本地 SoapySDRPlay3 或 SDRplay API 二进制。MIT 驱动和厂商 API 由用户按各自条款
在接收机电脑单独安装。此前 API 3.15 installer-v2/EULA 调查仍为有效历史证据，
不表示取得新的再分发或链接授权。独立服务边界不将此前直连条件带入基础包或客户端闭包。
SDRconnect 评估、服务安装、协议及验收边界见[接收机服务](docs/receiver-services.zh-CN.md)。

**Pager 嵌入式固件：** Arduino-ESP32、ESP-IDF、RadioLib、工具链及 SDK 二进制库
属于独立固件边界。固件发布前须核实条款与实际链接映射；桌面授权不会把整个 SDK
改为 GPL，也不授予无线固件二进制权利。本次许可改造的桌面包不分发固件镜像。

**工具、夹具和数据：** 编译工具与可选维护用 PDF 提取工具不属于桌面运行时。
现有可选 PyMuPDF 表格提取器具有 AGPL／商业条款，不执行、不随包交付，也不是
生产源码构建的前提。外部标准 PDF、测试录音、用户采集与分析输出不因根目录 GPL
文件而获得新许可；夹具已有来源说明与 CC0 声明继续适用。

## 保持声明准确

提供者在构建／分发组装位置登记许可根。经过评审的平台配置固定源码哈希、许可
替代项、运行库所有者和构建事实。通用收集器检查材料存在与哈希、各包依赖闭包、
实际可执行文件所有者与未决条件；签名器重新打开归档复查。新平台版本和新运行库
须补充评审证据，不能复制旧清单蒙混通过。机械检查不会裁定有争议的版权，也不能
代替缺少的授权。
