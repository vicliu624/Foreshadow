# 免费开源分发的编解码专利评估

[English](codec-patent-assessment.md) · 简体中文

评估日期：**2026-11-14**。范围是拟在 GitHub 免费公开下载的 GPL-3.0-or-later
Windows x86-64／Debian 13 x86-64 桌面源码与二进制，以及本地电视解码；不评估收费整机、
硬件捆绑、编码服务或广播节目再分发。全球下载不视为只在中国境内分发。
实际协议中的发行主体、关联方、地域和数量仍需确认；免费发布不向 GPL 添加非商业限制。

**结论：**现有公开资料未建立适用于本分发的一揽子专利豁免，也不能据此断定 Foreshadow
必须向所有列举的专利池付费或实施了某项有效权利要求。仍须考虑权利要求、法律状态、地域、
行为与已取得的授权。完整基础包已保留功能并准备产物，其编解码专利决策仍未解决。
单独交付的 SDRplay 网络客户端运行时不包含或调用这些电视实现，发布决策单独处理。
随包生产源码归档是源码超集，不表示其中每个文件均链接到客户端。

## 实际评估的实现

原生检查确认新包的 `avcodec` 在两平台均注册以下解码器：

`cavs`、`h264`、`hevc`、`mpeg2video`、`mpeg4`、`aac`、`aac_latm`、`ac3`、`eac3`、
`mp2`、`mp3`、`pcm_s16le`。

运行库报告 LGPL 2.1-or-later，构建参数含 `--disable-gpl --disable-nonfree`。
本地 AVS+ 改造随固定 FFmpeg 构建；MIT DRA 核心另行编入隔离媒体辅助进程，
不是 FFmpeg 注册解码器。本轮修正文档没有增删解码器、编码器或运行库。
见[构建配置](../cmake/BuildFfmpeg.sh)、[DRA 来源](../third_party/dra/UPSTREAM.zh-CN.md)
及[新包证据](reports/2026-11-14-public-release-preparation-proof.json)。著作权许可与进程隔离
都不能单独判断专利适用性。

## 一手资料核实结果

| 实现 | 公开证据 | 对本次免费分发的判断 |
|---|---|---|
| AVS／AVS+ | [2021 年 AVS 知识产权政策](https://www.avs.org.cn/public/storage/upload/files/download/IPRPolicy.pdf)第 12／14 条区分 RAND-RF、专利池与 RAND，第 30 条针对参考软件著作权。 | 未建立通用免费开源专利豁免；需识别适用标准、权利要求及可用授权。 |
| DRA | [2008 年由当时 CMMB 授权代表供稿的说明](https://www.cnr.cn/2008tf/CMMB/js/200808/t20080807_505059772.html)提到书面许可及截至 2009 年 6 月的免版税期。 | 仅为历史、限定领域证据，不是当前桌面 GitHub 二进制授权；免费分发权仍未确认。 |
| AVC／H.264 | [Via LA 当前项目](https://www.via-la.com/licensing-programs/avc-h-264/)涉及软件编解码器及初始零版税档，实际协议决定范围。 | 零版税档不证明免签协议；不假定下载计数或本项目符合条件。 |
| HEVC／H.265 | [Access Advance 当前 FAQ](https://accessadvance.com/faq/)明确回答免费下载软件仍需个案判断；[原 Via 项目](https://www.via-la.com/licensing-programs/hevc-vvc/)现由 VCL Advance 管理。 | 免费价格本身不能证明豁免，还需核实相关组合及授权重叠。 |
| AAC | [Via LA FAQ](https://www.via-la.com/licensing-programs/aac/)区分编解码产品与编码后码流分发，PC 软件计费分类须获认可。 | Foreshadow 分发解码实现，不只是码流；未找到适用的一揽子豁免。 |
| MPEG-2 视频／系统 | [当前专利池页面](https://www.via-la.com/licensing-programs/mpeg-2/)说明仍有马来西亚覆盖。 | 美国到期不能外推全球；也不据此断言本实现落入某项权利要求。 |
| MP3 | [Fraunhofer 说明](https://www.audioblog.iis.fraunhofer.com/mp3-software-patents-licenses)确认核心专利项目因到期而结束，另区分实现特定权利。 | 已结束的项目本身不是新增许可阻断，但不写成全球全部专利保证。 |
| AC-3、E-AC-3、MPEG-4 Part 2、MP2 | 实际存在。[Dolby 许可入口](https://professional.dolby.com/licensing/)不是免费软件豁免，也不是对本独立实现的权利要求分析。 | 不断言新的许可义务或豁免；实现／地域分析尚不完整。本轮未发现 PCM 的独立编解码专利池问题。 |

[2006 年 AVS 专利池建议](https://www.avs.org.cn/index/list?catid=48)是有起动期限的旧指导，
常见“一元”说法不是 Foreshadow 当前费率或许可。旧 DRA 芯片／硬件条款、单件专利无效或
标准年代也不能清理整个软件实现。[单独的 AVS2 建议](https://www.avs.org.cn/index/list?catid=49)
提到可有免费许可档次，这是该标准可核实的积极路径，不是本项目 AVS／AVS+ `cavs` 实现的
许可。不能混同 AVS2／AVS3 与 AVS+；应明确询问软件豁免是否覆盖我们的标准和分发方式。
本轮未取得当前官方权利要求登记结果：
[CNIPA 查询入口](https://cpquery.cponline.cnipa.gov.cn/chinesepatent/index)返回 HTTP 412，
AVS 许可门户超时。这些是查询限制，不证明专利有效、无效或到期。

## 免费研究软件为何不能自动认定豁免

[中国专利法](https://www.cnipa.gov.cn/art/2020/11/23/art_97_155167.html)第 11 条规定生产经营目的，
第 75 条第 4 项涉及专为科研实验使用；最高法的[裁判说明](https://ipc.court.gov.cn/zh-cn/news/view-1330.html)
指出，不能仅按营利或主体性质判断生产经营目的。这些资料没有裁判 Foreshadow。
我们的推论是：私人研究使用与公开分发可用解码软件需分别分析，叫作实验室或免费下载，
不足以宣称全部分发／使用均获豁免。

本文件是有证据的工程评估，不是实施自由法律意见。不据此推定必须付费、承认侵权或保证全球安全。
本轮没有发送许可请求、支付费用、签订协议或接受厂商条款。

## 保留功能并解决未决判断

准备的基础包保留电视采集／TS 处理、AVS+、DRA 及现有媒体辅助工具；没有用隐藏菜单、
下载免责声明或新增非商业 EULA 代替许可。SDRplay 客户端独立验收，要求宿主至少 0.2.12
及另装服务；厂商 API 与 Pager 固件不进入这两类下载。

剩余的具体问题是：所述免费桌面二进制分发及 GPL 下游权利，是否不落入相关有效权利要求、
已有覆盖，或可取得兼容的免费授权。优先明确 AVS+／DRA，再处理实际包含的 HEVC／AVC／AAC。
若需协议，接受前核实真正条款，包括地域、申报、下游再分发及 GPL 兼容性；不预设必须付费，
也不预设可接受逐份或限制性条款。

### 权利人或专利顾问询问范围草稿

以下为待使用文本，**尚未发送，也不是承认侵权**：

> Foreshadow 是 GPL-3.0-or-later 信号研究桌面应用，拟通过 GitHub 免费分发 Windows x86-64
> 与 Debian 13 x86-64 源码／二进制，在本地解码用户接收或提供的电视流。当前实现包含本次
> 询问对应的编解码族，可提供精确源码修订、构建参数及解码器清单。本范围不分发电视节目，
> 不经营编码或流媒体服务。
>
> 请确认现行许可管理方，以及您认为与该具体分发有关的有效专利权利要求和地域。免费开源
> 下载是否需要专利协议？是否有免费或零版税路径，有何条件／申报要求？授权能否覆盖符合
> GPL 的源码／二进制再分发、修改版及下游用户，不附加非商业或逐接收者限制？请提供当前
> 适用条款及任何豁免或已有覆盖的依据。我们仅寻求澄清，尚未接受协议。

使用草稿时不得编造下载量、公司身份、收入、广告或硬件捆绑事实。源码／材料门禁继续检查
其实际范围，不标成专利清权。
