# Foreshadow 改名、插件文档与评审版维护

[English](2026-11-13-foreshadow-maintenance.md) · 简体中文

日期：2026-11-13，Asia/Shanghai。冻结软件源码为 `d7cfd271c1ee19e022051676fac48c545dab7381`，文档、检查器及 CI 变更在其后整理。[结构化证据](2026-11-13-foreshadow-maintenance-proof.json)记录实际包与部署身份。这是本地评审维护，没有创建正式发布、标签或签名目录。

## 产品与架构说明

展示名称改为 **Foreshadow**，统一窗口/CLI 说明、资源、应用包组装、更新快捷方式和发布归档标签。公共命令/命名空间、`SignalScope` 状态目录、`signalscope:*` 元数据、`org.amc.ams` 等包 ID 及分析后缀保持兼容。历史产物名称与哈希保留原日期语境。

用户指出插件权重不足。README 已将产品定位为插件驱动的工作台，为功能逐项标注所有者，并增加中英文[插件指南](../plugins.zh-CN.md)。接收机、检测、显示、实时研究、模型、恢复和媒体提供者交付领域能力，公共协议/DSP、会话、存储、工作区与分发连接它们。运行时接收机、编译内置提供者、外部快照程序分别说明；工具箱是安装/版本单位，不表示所有注册实现都能单独下载。

## 文档与截图

清单包含 69 组中英文文档，共 138 份 Markdown，默认英文并提供双向导航。需求、协议、用户、构建、源码说明已回归当前实现；历史设计、决策和诊断保留日期。四份较长记录整理了英文时间线，保留实质正反例和性能证据；此前“无 Mac 环境”“仅显式 PSK 参考”等阶段与后续验收区分。

README/截图集包含 15 张英文原生截图，均来自连接接收机的 Debian 13 工作站。[来源清单](../images/provenance.json)记录输入类型、尺寸和 SHA-256。真实 RSPdx-R2 输入、设置、FM 状态与六份明确合成研究样例分开；电视播放仓库 CC0 合成 TS，设备静音，不声称当地天线节目实收。面板通过正常工作区操作整理。历史 Mac 截图留在对应日期报告中。

`tools/docs/check_docs.py` 检查清单、默认语言、对应文件、语言导航、本地链接、标题锚点及 PNG 文件头/尺寸，接入现有自托管 CI。独立临时夹具验证缺失语言版本、损坏链接确实被拒绝，另检查 Markdown 表格和空白。这些检查不证明翻译逐句等价或原生应用行为。

## Windows/Linux 验证与实际部署

| 检查 | Windows | Linux |
|---|---|---|
| 全量 CTest |100 项，99 通过，1 项既有音频条件跳过，0 失败|102 项，99 通过，3 项 SDRplay 替身隔离条件跳过，0 失败|
| 真实包 |应用/接收机/研究工具箱事务，中文/非 BMP 路径、发现、启动、解包桌面回归通过|相同检查通过|
| 完整归档比对 |178 文件与已验收解包树一致|118 文件一致|
| 评审根 |`C:/Users/vicliu/Apps/Foreshadow-Review-20261113-r1`|`/home/vicliu/Applications/Foreshadow-Review-20261113-r1`|
| 入口 |桌面 `Foreshadow Review`|应用菜单 `Foreshadow Review`|
| 启动语言 |显式 `--language en`|显式 `--language en`|
| 本次启动 PID |7808|183177|

包/部署 JSON 保留归档、哈希、来源；Linux 包元数据使用对应短提交 `d7cfd27`。安装后两个接收机模块与实时提供者发现通过。六份便携分析使用英文文件名，附中英文评审说明；新设置、事件、工具箱状态隔离，保留旧程序载荷。启动器没有截图退出或每次重复应用测试布局的参数。PID 仅为本次观察。

Windows 损坏可选 DLL 反例暴露真实系统“损坏的映像”模态框，阻塞启动。加载器临时设置线程级 `SEM_FAILCRITICALERRORS` 后恢复原模式，沿用通用拒绝诊断，没有设备 ID 分支或延长超时。针对性及最终全量回归通过。相关系统契约见 [Microsoft 加载器文档](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw)和[线程错误模式](https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setthreaderrormode)。原失败日志保留在私有缓存。

## 新版 Mac：复验失败，未计为通过

改名后的 Intel Mac 源码构建成功，首次 102 项回归为 94 通过、8 失败；八项串行复验三项通过，仍有五项失败：`gui_replay_smoke`、`media_playback_contract`、`dtmb_tv_provider_contract`、`dtmb_tv_actual_media`、`fm_playback_recovery`。

日志包括 `AudioOutputUnitStart` 错误 35/`Qt::ApplicationInactive`、媒体无进度及流程超时。原因尚未确定，不归为已证明修复或平台外部跳过；没有放宽门槛/断言。前置检查失败后未推进新包及部署门禁。原已验收 Mac 评审部署保持，其 `b06c3b5` 的 102/102 不能替新版 Foreshadow 应用包背书。

私有 Mac 日志位于 `/Users/liuweikai/.cache/amc-waveform-01a1c9fc-macos/logs/`，包括 `foreshadow-tests-first-failure.log`、`foreshadow-LastTest-first-failure.log`、`foreshadow-focused-serial.log`。Windows/Linux 日志和归档保留在各任务缓存。凭据、私钥及私有 RF 输入不进入仓库。

本轮已验证文档/链接/图片结构、Windows/Linux 全量回归/真实包/部署和接收机工作站截图。新版 Mac 包及部署、最新分支 Apple Silicon、广泛系统覆盖、人工听感、RF 校准及长期硬件无损尚未通过或未验证。本次维护不扩大既有实机验收的输入与时长范围。
