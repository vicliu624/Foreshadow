# Foreshadow 源码地图

[English](README.md) · 简体中文

下列路径相对于仓库根目录。Foreshadow 是产品名称；`signalscope`/`amc` 目标名及公共命名空间保留为兼容标识。

| 职责 | 实现 | 构建目标/组装 |
|---|---|---|
| 样本编解码、原生路径、来源窗口与流 | `src/backend/iq/`，公共协议在 `include/signalscope/backend/iq/` | `signalscope_iq` |
| 通用 DSP | `src/backend/dsp/`：滤波/分信道、有理重采样、波形、窗口分析、参考拟合、恢复校验、来源支撑及导出 | `signalscope_dsp` |
| 会话与接收租约 | `src/backend/sessions/`：配置、监听、回放、快照、状态与独立接收需求 | `signalscope_core` |
| 录制与事件库 | `src/backend/recordings/`：有界录制、预录内存、所有权、配额、持久化 | `signalscope_core` |
| 接收机协议/加载 | `src/adapters/receivers/` | `signalscope_receivers`：运行时 ABI、拒绝诊断及公共入口 |
| 具体接收机提供者 | `plugins/receivers/rtl_sdr/`、`sdrplay/`，独立夹具 `test_receiver/` | `amc-receiver-rtlsdr`、`amc-receiver-sdrplay`，拥有设备规则和资源 |
| 检测 | `src/adapters/modulation/`：LoRa、窄带 FSK/OOK/ASK、FM、CSS、PSK、OFDM、DTMB，通用注册和快照调度 | `signalscope_modulation`，`registry/` 组装 |
| 参考模型实现 | `plugins/research/models/` | `registry.cpp`：提供者参数、校验、生成和资源 |
| 恢复实现 | `plugins/research/recovery/` | `registry.cpp`：有界 FSK、LoRa PHY、成对帧族、训练式 PSK/OFDM |
| 桌面宿主 | `src/frontend/desktop/`：主窗口、菜单、工作区、通用工具承载、接收机控件、错误及本地化 | `signalscope-gui` |
| 公共参数控件 | `plugins/desktop/common/` | `signalscope_desktop_parameters`：提供者描述与整套草稿联动 |
| 显示提供者 | `plugins/desktop/display/` | `signalscope_desktop_display` |
| 桌面研究提供者 | `plugins/desktop/research/`：内置工具、波形实验室、后台、通用模型/恢复面板和外部工具宿主 | `signalscope_desktop_research`，`live_registry.cpp` 组装实时提供者 |
| 外部研究示例 | `plugins/external/power_report/` | `amc-power-report`：声明资源的独立原生工具箱 |
| 电视/媒体 | `src/adapters/television/`、`src/adapters/media/`、`src/backend/transport/`、`src/backend/media/` | 有界原生辅助进程，固定依赖由 `cmake/` 组装 |
| 分发领域 | `src/backend/distribution/` | 目录信任、兼容、安装记录/事务、激活协议 |
| 网络适配 | `src/adapters/distribution/` | Qt 传输/系统代理，与目录及安装逻辑分离 |
| 分发工具 | `tools/distribution/` | `signalscope-toolboxes`、`signalscope-update`、稳定启动器、打包/签名 |
| CLI | `src/frontend/cli/` | `signalscope`：命令组装与公共服务调用 |
| 测试固件/主机工具 | `firmware/pager_tx/`、`tools/pager/` | 独立有界测试发射器，普通测试/重放不发射 |

## 插件所有权与宿主基础设施

接收机、检测、显示、实时研究、模型、恢复和媒体提供者实现大部分产品功能。共享内核与宿主服务连接它们。提供者拥有规则、参数、资源 key 与结果；目录名不能决定其属于运行时 ABI、内置注册还是外部进程。产品映射和交付区别见[插件指南](../docs/plugins.zh-CN.md)。

## 依赖方向

桌面/CLI 调用会话服务及公共提供者协议。厂商库限定于接收机提供者组装。检测器可以使用公共 DSP，DSP 不导入具体接收机、检测器或会话编排。来源/流协议位于会话之下，研究实现不必反向依赖会话。

模型生成与接收算法属于研究提供者，通用对照/恢复内核检查类型数据、预算及来源支撑。界面、导出、保存后台及证据浏览器消费协议，不按模型/恢复 ID 增加行为分支。新结果能力需要明确升级协议。

根 CMake 组装模块；`cmake/ModuleLinkRules.cmake` 约束目标依赖，`tests/check_module_dependencies.cmake` 检查直接头文件依赖，`specs/extension-contracts.json` 定义机器约束。这些检查补充独立提供者行为测试。

## 扩展协议

- [接收机 ABI](../docs/15-接收机插件协议与扩展.md)：发现、参数、实际值、运行库和资源声明。
- [扩展边界](../docs/16-扩展边界与开闭原则规格.md)：变化所有权及稳定消费者要求。
- [工作区/实时研究](../docs/18-桌面工作区与模块界面规格.md)：`LiveResearchProvider`、面板、租约、生命周期、保存布局。
- [波形实验室](../docs/22-波形实验室与可追溯分析实施规格.md)：分段窗口、原生路径、时钟及分析持久化。
- [参考模型](../docs/23-参考模型与对照协议规格.md)：提供者生成/参数及公共拟合。
- [恢复协议](../docs/24-信号恢复与证据协议规格.md)、[复数证据/EVM](../docs/25-PSK与OFDM接收及EVM规格.md)：符号、字节、帧、测量、追溯。
- [分发](../docs/17-受控工具箱与在线更新规格.md)：签名目录、包类型适配和公共事务。

桌面语言元数据位于 `src/frontend/desktop/i18n/`，Qt 翻译在 `translations/`；提供者文案在各自 `i18n/`，公共样本/文本层负责查找及回退。`ams_zh_CN.ts` 等名称保留为内部兼容细节。

外部研究工具使用版本化快照 IPC、资源与参数声明，不会自动获得原生实时/多面板能力。内置 C++ 实时提供者与可独立安装接收机 ABI 模块遵循不同交付协议。
