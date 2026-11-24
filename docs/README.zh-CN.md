# Foreshadow 文档索引

[English](README.md) · 简体中文

Foreshadow 是以插件驱动的信号研究工作台。接收机、检测、显示、实时研究、模型、恢复与媒体提供者交付大部分领域功能；宿主提供公共协议、会话、存储、工作区和受控分发。

当前文档说明源码分支。有日期的验收报告绑定各自源码、包与硬件，正式发布可能具有不同基线；失败、跳过及外部条件须与结果一起阅读。

- [许可范围](../LICENSING.zh-CN.md)、[第三方声明](../THIRD_PARTY_NOTICES.zh-CN.md)和[源码／替换说明](../SOURCE_AND_REPLACEMENT.zh-CN.md)。
- [版权声明](../COPYRIGHT.zh-CN.md)与[品牌使用说明](../BRANDING.zh-CN.md)，保留独立分发、衍生版本及插件的 GPL 权利。

- [分发许可材料协议](distribution-compliance.zh-CN.md)。

- [GPL 落地与 Windows/Linux r3 评审验收](reports/2026-11-13-license-adoption-review.zh-CN.md)。

## 从这里开始

- [产品概览与完整流程](../README.zh-CN.md)
- [插件分类、宿主职责与交付形式](plugins.zh-CN.md)
- [接收机工作站上的英文截图](screenshots.zh-CN.md)
- [11 月 24 日 RSPdx-R2 实机流程、插件生命周期及本地评审修复](reports/2026-11-24-rspdx-r2-workflow-review.zh-CN.md)
- [交付与使用指南](09-交付与使用说明.md)
- [构建与 CLI](07-构建与命令行操作.md)
- [源码所有权地图](../src/README.zh-CN.md)
- [许可证评估、Qt 范围与分发缺口](licensing-assessment.zh-CN.md)
- [实际免费开源编解码专利评估及询问范围](codec-patent-assessment.zh-CN.md)
- [工程约束](../AGENTS.zh-CN.md)

## 当前需求与协议

| 文档 | 状态 |
|---|---|
| [产品需求与验收](01-产品需求与验收.md) | 当前说明 |
| [架构与接口设计](03-架构与接口设计.md) | 当前说明 |
| [算法与测试方案](04-算法与测试方案.md) | 当前说明 |
| [实施状态与风险决策](05-实施计划与风险决策.md) | 当前说明 |
| [构建与命令行操作](07-构建与命令行操作.md) | 当前说明 |
| [长期监听与事件存储](08-长期监听与事件存储.md) | 当前说明 |
| [交付与使用说明](09-交付与使用说明.md) | 当前说明 |
| [接收机能力与配置联动](13-接收机能力与配置联动.md) | 当前说明 |
| [宽带接收与多速率分析](14-宽带接收与多速率分析.md) | 当前说明 |
| [接收机插件协议与扩展](15-接收机插件协议与扩展.md) | 当前说明 |
| [扩展边界与开闭原则规格](16-扩展边界与开闭原则规格.md) | 当前说明 |
| [受控工具箱与在线更新](17-受控工具箱与在线更新规格.md) | 当前说明 |
| [桌面工作区与模块界面](18-桌面工作区与模块界面规格.md) | 当前说明 |
| [DTMB 识别](19-DTMB识别规格.md) | 当前说明 |
| [DTMB 节目流与电视播放](20-DTMB节目流与电视播放规格.md) | 当前说明 |
| [独立广播会话与状态栏](21-独立广播会话与状态栏规格.md) | 当前说明 |
| [波形实验室与可追溯分析实施规格](22-波形实验室与可追溯分析实施规格.md) | 当前说明 |
| [参考模型与对照协议](23-参考模型与对照协议规格.md) | 当前说明 |
| [信号恢复与证据协议规格](24-信号恢复与证据协议规格.md) | 当前说明 |
| [PSK / OFDM 接收与 EVM](25-PSK与OFDM接收及EVM规格.md) | 当前说明 |
| [macOS 原生构建与部署验收](26-macOS原生构建与部署验收规格.md) | 当前说明 |

## 历史设计与研究背景

| 文档 | 状态 |
|---|---|
| [硬件与环境调查](02-硬件与环境调查.md) | 历史记录 |
| [中国 LoRa 应用与监听预设](06-中国LoRa应用与监听预设.md) | 历史记录 |
| [821 MHz 接收迁移需求](10-821MHz接收迁移需求.md) | 历史记录 |
| [Sub-GHz 调制识别与物业信号调查](11-Sub-GHz调制识别与物业信号调查.md) | 历史记录 |
| [跨平台桌面与接收机设计](12-跨平台桌面与接收机设计.md) | 历史记录 |

## 有日期的验收与调查报告

| 文档 | 状态 |
|---|---|
| [首次采集与环境记录](reports/2026-11-03-首次采集与环境记录.md) | 历史记录 |
| [低功率短包漏检诊断（进行中）](reports/2026-11-04-低功率短包漏检诊断.md) | 历史记录 |
| [待验证候选的被动调查](reports/2026-11-04-待验证候选调查.md) | 历史记录 |
| [LoRa 恢复实测与弱信号基线](reports/2026-11-05-LoRa恢复实测与弱信号基线.md) | 历史记录 |
| [接收机设置与调制评估验收](reports/2026-11-08-接收机设置与调制评估验收.md) | 历史记录 |
| [接收机配置第一批验收与 uConsole 部署](reports/2026-11-08-接收机配置第一批验收.md) | 历史记录 |
| [FM 检测与实时流水线验收](reports/2026-11-09-FM检测与实时流水线验收.md) | 历史记录 |
| [双接收机宽带改造验收](reports/2026-11-09-双接收机宽带验收.md) | 历史记录 |
| [在线更新与受控工具箱发布验收](reports/2026-11-09-在线更新与工具箱发布验收.md) | 历史记录 |
| [接收机插件与开闭原则验收](reports/2026-11-09-接收机插件与开闭原则验收.md) | 历史记录 |
| [FM 播放恢复与模块控制验收](reports/2026-11-10-FM播放恢复与模块控制验收.md) | 历史记录 |
| [LoRa 实时丢弃与共享调度验收](reports/2026-11-10-LoRa实时丢弃与共享调度验收.md) | 历史记录 |
| [DTMB 电视播放与 0.2.10 发布验收](reports/2026-11-11-DTMB电视播放与0.2.10发布验收.md) | 历史记录 |
| [DTMB 白天复扫与候选复核](reports/2026-11-11-DTMB白天复扫.md) | 历史记录 |
| [DTMB 节目解码阶段验证](reports/2026-11-11-DTMB节目解码阶段验证.md) | 历史记录 |
| [DTMB 识别与实收扫描验收](reports/2026-11-11-DTMB识别与实收扫描验收.md) | 历史记录 |
| [波形实验室实施与验收记录](reports/2026-11-11-波形实验室实施记录.md) | 历史记录 |
| [PSK / OFDM 接收、均衡与 EVM 验收](reports/2026-11-12-PSK与OFDM接收验收.md) | 历史记录 |
| [macOS 原生部署与接收流程验收](reports/2026-11-12-macOS原生部署与接收流程验收.md) | 历史记录 |
| [波形实验室完成条件审计](reports/2026-11-12-波形实验室完成条件审计.md) | 历史记录 |
| [Foreshadow 改名、插件文档与评审版维护](reports/2026-11-13-foreshadow-maintenance.zh-CN.md) | 历史记录 |
| [PCM 回放与许可证评估验收](reports/2026-11-13-pcm-and-licensing-review.zh-CN.md) | 历史证据 |
| [GPL 采用与 Windows/Linux r3 评审验收](reports/2026-11-13-license-adoption-review.zh-CN.md) | 历史证据 |
| [独立基础包、接收机服务与 Windows/Linux r4 验收](reports/2026-11-14-receiver-service-review.zh-CN.md) | 历史证据 |
| [源码命令修正、新归档及分别准备公开发布](reports/2026-11-14-public-release-preparation.zh-CN.md) | 历史证据 |

## 决策记录

| 文档 | 状态 |
|---|---|
| [条件 SFD 容差与弱信号候选](adr/0001-条件SFD容差与弱信号候选.md) | 决策 |

## 固件、夹具与依赖来源

| 文档 | 状态 |
|---|---|
| [Pager 串口可控低功率 LoRa 与 GFSK 测试固件](../firmware/pager_tx/README.zh-CN.md) | 当前说明 |
| [v3 周期 FSK 方向变化修复候选](../patches/v3-periodic-fsk/README.zh-CN.md) | 历史记录 |
| [v4 弱信号研究快照](../research/v4/README.zh-CN.md) | 历史记录 |
| [反斜杠归档路径拒绝夹具](../tests/fixtures/distribution-backslash.zh-CN.md) | 夹具来源 |
| [独立离线 DTMB 波形](../tests/fixtures/dtmb-waveform/README.zh-CN.md) | 夹具来源 |
| [弱边缘联合前导码回归](../tests/fixtures/lora-edge-high-coherent-prefix/说明.md) | 夹具来源 |
| [高频边缘 SFD 转换漏检录制](../tests/fixtures/lora-edge-high-sfd-transition/说明.md) | 夹具来源 |
| [低侧边缘时频细化回归](../tests/fixtures/lora-edge-low-fine-sync/说明.md) | 夹具来源 |
| [受控射频弱下 Chirp 回归样本](../tests/fixtures/lora-sf7-weak-sfd/说明.md) | 夹具来源 |
| [周期 FSK 的 v3 误判反例](../tests/fixtures/periodic-fsk-v3-false-1/说明.md) | 夹具来源 |
| [周期 FSK 的 v3 误判反例](../tests/fixtures/periodic-fsk-v3-false-2/说明.md) | 夹具来源 |
| [合成电视节目](../tests/fixtures/television-programs/README.zh-CN.md) | 夹具来源 |
| [原生电视依赖](../third_party/README.zh-CN.md) | 当前说明 |
| [DRA 解码核心来源](../third_party/dra/UPSTREAM.zh-CN.md) | 当前说明 |
| [对固定 dtmb-sdr 源码应用的修正](../third_party/dtmb-patches/README.zh-CN.md) | 当前说明 |

## 文档维护

每份 Markdown 维护英文默认版与简体中文版，由 `documents.json` 索引。同步修改两份，保留历史身份，运行 `python tools/docs/check_docs.py` 检查清单、语言导航、链接、锚点与 PNG。

- [接收机服务与独立基础包](receiver-services.zh-CN.md)：当前 SDRplay 服务边界、安装、控制及连续性限制。

- [仓库迁移与本轮源码发布](repository-transition.zh-CN.md)
