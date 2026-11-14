# 对应源码与运行库替换

[English](SOURCE_AND_REPLACEMENT.md) · 简体中文

通过验收的包均携带 `share/foreshadow/compliance/`。先查看 `index.html`、
`inventory.json` 和 `verification.json`。源码是与原始声明一起提供的真实归档，
不会用主页链接代替。源码提交、平台版本、材料 SHA-256、运行库路径和未决条件
共同标识本次交付。项目不额外限制为调试 LGPL 库修改所需的修改或逆向工程。

## 源码内容

`foreshadow/source/` 归档包含冻结的生产源码、全部自研插件、SDK 头文件、资源、
CMake 安装规则和维护／构建脚本。`SOURCE-DELIVERY.json` 记录 Git 提交及省略的
非生产研究归档、历史补丁、外部测试夹具及单独评审的嵌入式固件。这些资产不是 `BUILD_TESTING=OFF`
构建的必要输入；完整仓库保留历史及来源，生成交付归档不会删除它们。

各依赖目录包含固定的原始上游归档、适用的发行补丁／构建规则及原始声明。
Windows vcpkg 材料包括实际 port 定义、补丁、triplet、脚本、基线和已安装依赖元数据。
Qt 包含模块源码、上游构建信息及 SDK SPDX 元数据。Debian 包含精确版本的 `.dsc`、
原始 tarball 与 Debian 补丁归档，使用 `dpkg-source -x` 时保留该源码包全部文件。
记录 HTTPS 来源与 SHA-256 校验；未实际验证上传者签名时，不宣称 `.dsc` 已验签。

## 重新构建

需要 CMake 3.20+、C++20 编译器及[构建指南](docs/07-构建与命令行操作.md)中的依赖。
Windows 评审配置为 MSVC 2022、Qt 6.8.3 和固定的 vcpkg 源码／port；Linux 为
Debian 13、GCC 与 Qt 6.8.2 源码包。OpenSSL 至少为 3.0。换用其他依赖版本时，
不能继续声称复现了当前交付的精确版本。

1. 解压 Foreshadow 与所需依赖源码。Debian 使用 `dpkg-source -x package.dsc`
   解包并应用发行补丁，需要时安装 `dpkg-dev`。Windows 使用交付的 vcpkg port、
   补丁、triplet 元数据重建依赖。编译器和操作系统 SDK 是构建前提，不是重新授权
   为 Foreshadow 的组件。
2. 按附带源码与规则构建／安装 Qt 和原生库；修改版也可使用 ABI 兼容的开发包。
   Qt 平台插件、模块集合须与相应 Qt 构建一致。
3. 为 FetchContent 明确指定解压目录。以下路径均需替换为本机实际位置：

```sh
cmake -S foreshadow -B build -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DSIGNALSCOPE_REQUIRE_GUI=ON -DSIGNALSCOPE_REQUIRE_AUDIO=ON \
  -DFETCHCONTENT_SOURCE_DIR_JSON=/sources/json \
  -DFETCHCONTENT_SOURCE_DIR_KISSFFT=/sources/kissfft \
  -DFETCHCONTENT_SOURCE_DIR_DTMB_DECODER=/sources/dtmb-sdr \
  -DFETCHCONTENT_SOURCE_DIR_FFMPEG_NATIVE=/sources/ffmpeg \
  -DFETCHCONTENT_SOURCE_DIR_SOAPYREMOTE=/sources/SoapyRemote
cmake --build build --config Release --parallel 4
cmake --install build --config Release --component SignalScope --prefix /output
cmake --install build --config Release --component ResearchExamples --prefix /output
```

Windows 在 x64 Native Tools 命令行使用本机路径，传入
`-DCMAKE_TOOLCHAIN_FILE=.../scripts/buildsystems/vcpkg.cmake`、交付的 triplet 与
`-DSoapySDR_DIR=.../share/soapysdr`。原生 FFmpeg 还需要 Bash、Make、NASM；
`cmake/MediaDecoder.cmake` 和 `cmake/BuildFfmpeg.sh` 保留精确解码器集合及移植补丁。
`cmake/DtmbReceiver.cmake` 应用附带的 DTMB 声明式补丁。生产构建不需要 PyMuPDF、
外部标准 PDF 或 RF 采集。这里的复现指源码／构建对应及功能重建，不保证编译时间戳
与签名逐字节相同。

## 替换 Qt 或 FFmpeg

先复制便携安装目录，停止该副本的程序并保留原目录以便回退。Foreshadow 不以签名
锁定 Qt／FFmpeg 动态库，也不在启动时拒绝用户修改的兼容库。在线目录签名验证下载包，
不会取消本地构建和运行修改版的能力。受管包更新可能覆盖修改，应保留独立修改副本。

- **Windows Qt：** 用架构、编译器运行时、模块配置及 ABI 兼容的动态构建替换
  `bin/Qt6*.dll` 和 `plugins/` 中匹配插件，同时提供匹配依赖。不要混合 Debug Qt
  与 Release 主程序。本次 Widgets／PCM 配置不携带 Qt 编解码插件和未使用的软件
  OpenGL；必须保留所需 Windows 平台插件。
- **Linux Qt：** Qt 来自系统。把兼容动态构建安装到单独前缀，给应用副本设置
  `LD_LIBRARY_PATH` 指向其 lib、`QT_PLUGIN_PATH` 指向其插件，或重新构建主体。
  避免插件路径混入无关版本；用 `/proc/<pid>/maps` 或加载器诊断核实实际加载路径。
- **FFmpeg：** 成套替换原生解码器的 avcodec、avformat、avutil、swresample、swscale
  动态库，Windows 在 bin、Linux 在 lib。公共 ABI 改变时重建主体／媒体辅助程序，
  并重跑解码和回放检查。启用 GPL／nonfree 功能的 FFmpeg 需要重新评审，附带 LGPL
  声明仅描述记录的配置。
- **静态依赖：** 使用附带源码和 port 补丁重建，再将附带自研源码重新链接。
  Windows RTL-SDR／libusb 也走此路径，放置新 DLL 不会替换模块内已有的静态代码。

这些说明描述机制与前提，不保证任意修改仍保持 ABI 兼容。macOS 原生替换、签名和
应用包验收不属于当前 Windows/Linux 验证范围。

## 再生成与发布检查

`tools/distribution/make_source_bundle.py` 生成冻结源码；`collect_materials.py`
使用经过评审的平台配置及哈希校验的缓存；`compliance.py` 把材料装入新的 CMake
安装树。`package_release.py` 按提供者声明为每个独立包嵌入适用依赖闭包。
`--review-only` 仅用于明确记录未决条件的私下评审；`sign_catalog.py` 重新打开每个
归档，拒绝缺失／变化的材料、未知运行库和未解决的分发条件。通用安装器不解释
许可证 ID 或提供者身份。
扩展已有签名目录时，保留的旧版本也须提供归档与条目文件以便重新验收，不能静默
继承加入本次检查之前的包；只要继续提供这些版本，就须在仓库保留对应的平台评审配置。

```sh
python tools/distribution/make_source_bundle.py --commit COMMIT --output source.tar.gz
python tools/distribution/collect_materials.py --profile PROFILE.json --cache CACHE \
  --first-party-source source.tar.gz --source-commit COMMIT --output MATERIALS
python tools/distribution/compliance.py --stage STAGE --materials MATERIALS --source-commit COMMIT
python tools/distribution/package_release.py --stage STAGE --output PACKAGES \
  --base-url RELEASE_URL --source-commit COMMIT
```

平台配置未经评审或缺少源码都会使发布失败。评审选项不能绕过声明、哈希、源码和
运行库所有者检查。

## 可选接收机客户端与独立服务

基础归档排除 SDRplay 客户端。独立 GPL 工具箱只链接 BSL SoapySDR 核心及固定的
SoapyRemote 客户端，随包交付上游源码归档和 `cmake/RemoteReceiver.cmake`。
测试构建的上游服务不会安装进应用，客户端不携带或加载厂商 API、硬件驱动。
独立服务分别构建、替换并遵守自身依赖及厂商条款。安装和原生数据边界见
[接收机服务](docs/receiver-services.zh-CN.md)。
