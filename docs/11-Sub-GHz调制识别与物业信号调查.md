# Sub-GHz 调制识别与物业信号调查

[English](11-subghz-modulation-research.md) · 简体中文

2026-11-05。新增识别范围限于 Sub-GHz 空口，面向水电计量、物业传感器、楼宇控制、无线门禁遥控和车钥匙遥控。现有 uConsole + AIO2 优先验证其直接可调谐的频段。125 kHz 近场 RFID 虽在 Sub-GHz 范围内，但属于独立的低频近场采集任务，当前 AIO2 接收链不覆盖。2.4 GHz 蓝牙和 Zigbee、Wi-Fi 以及 13.56 MHz 近场 NFC 不列入本轮实现。

## 监听与识别要求

| 场景 | 首批可研究的波形 | 验证方法 |
| --- | --- | --- |
| 水表、电表及楼宇传感器 | CSS／LoRa、窄带 FSK／GFSK、OOK／ASK 候选 | 先从 470–510 MHz 内选定连续窗口，记录突发频率、带宽、时长和重复周期。有已知设备样本时做开关机或事件时间对照 |
| 门禁遥控、车钥匙 | OOK／ASK、2-FSK／GFSK | 从 315 MHz 和 433.92 MHz 附近选研究窗口，用自有设备实测。通过包络电平、双频轨迹和重复突发识别 |
| 楼宇设备的其他 Sub-GHz 空口 | 视已确认设备而定的 FSK、O-QPSK 等 | 按设备规格、区域信道及样本决定频段与分类器，验证后才加入预设 |

首批结果按射频活动、调制候选、结构验证分层显示。CSS 分支记录扫频斜率、方向、扫宽和周期，LoRa 分支继续验证前导码和 SFD。OOK／ASK 分支验证窄带载波的包络电平和脉冲时序。FSK 分支验证多个频率状态、状态转换和符号节奏，初版对无法可靠区分的 FSK 与 GFSK 使用合并标签。调制类别与设备用途分开显示，来源归类需要已知设备触发、公开帧格式或其他独立证据。

单接收机使用一个固定连续窗口，切换预设时提示覆盖频率。保存短段 I/Q 和调制证据，候选诊断录制使用独立的小额度及过期淘汰。测试包含连续载波、噪声、周期 FSK、已知 CSS 和受控遥控样本。先验证离线重放，再验证 uConsole 的实时处理、虚警和存储预算。

## 478 MHz 被动采样

11 月 5 日约 22:33，在 Pager 未进行测试发射时，采集两段各 8 秒的 I/Q。中心分别为 478.5、478.7 MHz，采样率 1.75 MS/s，增益 38.6 dB，每段 1400 万对 I/Q。原始文件保存在本地 `recordings/meter-css-20261105-223329/`，离线检测用 v3.1、BW 自动、SF7–12、双极性和 11 dB 门限。

| 样本 | 原始候选 | 离线压制持续谱线后 | LoRa 结构匹配 |
| --- | ---: | ---: | ---: |
| 478.5 MHz | 4 | 4 | 0 |
| 478.7 MHz | 5 | 6 | 0 |

频谱用 8192 点加窗 FFT，分八段取中位功率。两段都在各自的接收中心看到谱线，换中心后跟随了 200 kHz，符合直流残留的特征。约 478.127、479.063、479.277 MHz 的谱线在两次采集中留在相近的射频位置。它们的来源还未确定。离线副本对选定持续谱线各抑制约 ±800 Hz，并重新量化成 CU8。第一段候选时间与模板保持一致，第二段部分候选改变。只压制这些谱线不足以解释全部探针。本轮没有建立可重复扫频或表计报文的证据，16 秒样本对低频率上报设备的覆盖有限。

工信部的民用计量仪表条目列出 470–510 MHz。Semtech 公开案例中有杭州水表采用 LoRa／LoRaWAN，以及工作在 470–510 MHz、同时支持私有 LoRa 和 LoRaWAN 的工业传感器。上述资料提供排查方向。现场归因仍需要独立的波形、协议或设备对照证据。

- [工信部微功率设备技术要求](https://wap.miit.gov.cn/cms_files/filemanager/oldfile/miit/n1146285/n1146352/n3054355/n3057735/n3057748/n8004149/c8006552/part/8006563.pdf)
- [Semtech 智能水表案例](https://www.semtech.com/lora/lora-applications/smart-water-metering)
- [Semtech 470–510 MHz 工业传感器案例](https://www.semtech.com/resources/customer-stories/enabling-lora-for-critical-monitoring-in-industrial-environments)
- [Microchip 汽车遥控 ASK／FSK 芯片资料](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-4615-ICs-for-Car-Access-Applications_Flyer.pdf)
- [Silicon Labs 楼宇与智能家居用 Sub-GHz 芯片资料](https://www.silabs.com/wireless/z-wave/efr32zg28-z-wave-800-socs)

当前优先扩展调制识别。双语界面、桌面框架替换和新硬件后端后置，详见跨平台设计记录。

2026-11-06：uConsole 已部署 AMS 实时 LoRa 与 2-FSK／GFSK 共采样检测。FSK 旁路为两块有界队列，在 Linux 用空闲调度优先级运行；采样分析积压时让行。ARM 300.70 秒实机验证中采集、分析和录制路径各处理 525000000 对 I/Q，LoRa 软件丢样、缺口及结果丢弃为零。FSK 分析 16017 块、让行 6 块。相同程序关闭及开启旁路的独立现场测量平均使用约 1.96 与 2.09 个 CPU 核，观察窗口分别为 30 与 300 秒。Pager 同一窗口交替实发 GFSK 3/3、LoRa 3/3；正式界面安装后各复测 1/1，LoRa 录制成功。FSK 当前是二频波形候选，不触发自动录制；同频或强弱悬殊时可能漏检。记录见 `build/rf-tests/ams-fsk-*-20261106.json`。

实时窄带旁路已扩展至 OOK 与 ASK，复用 FSK 已算的短时频谱，跨块只保留有界的峰值历史。持续同频载波在高低电平多次切换时产生候选；二频分支先验频率状态。ARM 300.70 秒验证处理 525000000 对 I/Q，LoRa 软件丢样、缺口及结果丢弃为零，窄带旁路分析 16023 块、跳过 0 块。程序平均 CPU 约 2.08 核，与上一轮约 2.09 核接近。Pager 交替发 OOK、ASK、GFSK、LoRa，候选和安装后均各检出一次。当前 FSK、OOK、ASK 均为波形候选，LoRa 仍独立按结构事件录制。记录见 `build/rf-tests/ams-ask-*-20261106.json`。

2026-11-06 实机对照：COM11 Pager 固件 `amc-pager-tx-0.3` 在 478.625 MHz 以名义 −9 dBm 发送一个 32 字节 GFSK 包，配置 2400 symbol/s、±4.8 kHz 频偏。uConsole AIO2 以 1.75 MS/s 录制两秒。ARM 和 Windows 上的离线宽带盲扫均找到 478.625183 MHz 附近的一段 `2-FSK/GFSK`，两个频率状态相隔约 8.35 kHz。当前验证的是波形分类，实时界面尚未接入。文件见本地 `recordings/pager-gfsk-1793924163618950900`。测试后恢复 LoRa 监听，Pager 返回 LoRa 待机。用户已接受 LoRa 常温观察并结束该验收，后台自动观察未跑满预定时长，日志保留。

## 首个离线分类入口

`python tools/classify_subghz.py --input capture.sigmf-meta --channel-hz 433920000 --width-hz 40000`

此命令接受 CU8 与 CI16_LE 录制，从指定的窄带信道提取突发，报告时间和 OOK、ASK、2-FSK/GFSK 或未知候选。OOK／ASK 检查包络电平和高电平载波稳定性，2-FSK/GFSK 检查近恒定幅度下的双频率状态。录制应包含突发前后的背景，供突发门限估计。

宽带自动发现使用 `python tools/classify_subghz.py --input capture.sigmf-meta --scan --width-hz 40000`。工具依据短时谱相对本频点背景的变化提取信道，再逐信道分类。默认分析最显著的 8 个候选，可用 `--max-channels` 调整至 16 个。当前门限偏保守，强干扰、靠近采集边界的信号、间隔过近的信道和持续占用都可能漏掉。每份录制上限 4194304 对 I/Q；实时集成待完成。

使用已有 Pluto 合成波形及改造对照，每类三段突发，OOK、ASK 和 2-FSK 分别 3/3 正确归入候选类别。恒定载波和噪声突发保留为未知，CSS 对照在所选窄带窗口内没有输出分类。Pluto 未发射本次样本。实物遥控、其他速率及低信噪比仍待验证。

混合录制回归 `python tests/subghz_scan_test.py` 使用同一段 3 MS/s I/Q，并发 OOK、2-FSK、4-FSK 各三段突发，叠加持续载波和短载波。自动发现三个调制信道和一个短载波信道，各调制信道分类 3/3，短载波保留为未知，持续载波未作为突发信道输出。指定信道入口在同一录制上仍通过。4-FSK 候选要求四个频率状态都呈现多次稳定停留，且间距近似均匀。

真实接收背景对照使用 478.5 MHz 被动录制的首秒，叠加 OOK 与 2-FSK 各两段突发，三档注入幅度下均正确检出。原始被动背景和包含真实 Pager LoRa 包的录制首秒均未输出窄带调制候选。这是离线注入对照，尚未验证遥控器的空口捕获。原始结果为 `build/rf-tests/subghz-real-background-20261105.json`。

离线分类已加入 BPSK 和 QPSK。先用二次方或四次方谱估计载波，在有限的候选符号速率和采样相位上检查稳定星座及状态切换，并要求相位跳变之间回到同一载波，减少和 FSK 的混淆。输出相位一致性和载波偏移，暂不提供可靠符号速率估计。当前速率候选为 1200、2400、4800、9600、19200 symbol/s，无法归类的突发仍保留为未知。

`python tests/subghz_psk_test.py` 使用 1.75 MS/s、两频点并发、有噪声与平滑过渡的合成录制，BPSK 和 QPSK 各检出三段。既有 OOK、ASK、2-FSK、4-FSK、恒定载波、噪声以及实收背景注入对照保持原结果。实际空口和其他整形仍待验证。

长录制可直接选时段，例如 `--scan --start-seconds 5 --duration-seconds 2`。工具只读取所选 I/Q，事件时间仍相对于原始录制开头。已验证可从 8 秒原始录制直接扫描第 5–7 秒；合成混合信道回归同时覆盖非零起点和事件时间。

补充 Pager −9 dBm 空口对照。实时 LoRa 检测器在窗口内和两侧边缘共检出 3/3，录制覆盖预计整帧。取三份录制各自含发射包的时段供新分类器盲扫，均未误标为 OOK、ASK、FSK 或 PSK。BW250/SF11 样本在窄带扫描中产生了许多“未知”片段，说明宽带 LoRa 的扫频可被窄带能量检测切碎；后续需要与宽带 CSS 证据结合，不能把这些片段当作多个设备发包。结果保存在 `build/rf-tests/subghz-lora-air-control-20261105/scan.json`。

联合扫描使用 `python tools/classify_subghz.py --input capture.sigmf-meta --scan --lora-exe build/Release/signalscope.exe`。此选项对同一份 CU8 数据运行 SignalScope LoRa 结构检测。确认事件包含前导码至 SFD 的时段，并可另报同频带的活动结束估计。只将同时落在带宽和时段内、且只与一个 LoRa 事件关联的未知片段归到该事件；已分类的突发保留。归并包含原始时间、信道和关系类型。SFD 后的归并是基于频带能量的关联，不代表载荷验证。

三段 Pager 空口对照各输出一个 LoRa 结构事件。BW250/SF11 对照中的 50 个未知片段归到同一个频带活动窗口，原始条目均保留。另在该录制相同时间叠加一个不同频点的合成 OOK，联合结果同时保留 LoRa 和 OOK。两组窄带回归继续通过。联合结果为 `build/rf-tests/subghz-lora-air-control-20261105/combined-scan.json`。

联合扫描同时输出 `css_candidates`。这一级要求至少六个重复扫频符号及足够的匹配能量集中度，单窗 Chirp 探针不进入此列表。同一模板的相邻重叠结果归并；与同模板 LoRa 结构事件匹配时，仅列出 LoRa 事件。CSS 候选保留时间、方向、候选带宽、等效 SF 和频率区间。当前复用已有 LoRa 参考模板，所以仅覆盖 BW125／250／500、SF7–12 等效斜率，未覆盖任意斜率或任意带宽。候选门限尚未做现场灵敏度标定。

`python tests/subghz_css_test.py build/Release/signalscope.exe` 验证三段通用 CSS 合成样本均只进入 CSS 层，OOK 和 2-FSK 对照不进入 CSS 层，真实高侧边缘 LoRa 只输出一个结构事件。三段此前 Pager 空口样本重跑也各只输出一个 LoRa 结构事件。

另加独立的 `--scan --blind-css` 实验选项。它在时频图中跟踪最强脊线，对多段线性扫频核对方向、斜率、频率位置、扫宽和重复周期，输出 `blind_css_candidates`。输出的观测扫宽只覆盖可靠跟踪到的部分，重复扫频证据也不足以确定具体调制协议或应用。

`python tests/subghz_blind_css_test.py build/Release/signalscope.exe` 验证两组非模板扫频，扫宽 187 kHz 和 213 kHz，周期 1.7 ms 和 2.3 ms。两组各识别八次，方向正确，斜率误差在 8% 以内。持续载波、2-FSK 和孤立扫频不产生重复扫频候选。三段 Pager 真实录制仍由 LoRa 分支各识别一次，独立脊线方法在这些样本中未输出候选。它适用于时频脊线明显、同一时段有一个主导信号的离线分析；弱包、并发扫频与其他时长还需扩展验证。

多分辨率更新后，在 1.75 MS/s 下使用 64、256 和 1024 点窗口，同一时频扫频由多种分辨率命中时只保留最佳结果。非模板回归新增 0.39 ms 的快扫和 6.5 ms 的慢扫，四组各识别八次，方向与斜率均通过。三段 Pager 录制均获得独立前导码扫频证据：BW250/SF11 由 1024 点窗口识别，两段 BW500/SF7 边缘包由 64 点窗口识别，各找到十次重复上扫频。与 LoRa 参数及前导码时间吻合的独立结果归到对应 LoRa 事件。SFD 后同带持续活动中的扫频仅作为关联证据。联合输出为 `build/rf-tests/subghz-lora-air-control-20261105/blind-css-multiscale-scan.json`。

两段 8 秒环境录制以 2 秒分析窗、50 ms 重叠覆盖，十个窗口均未见重复扫频候选。该结果只适用于这两段样本和当前门限。明细为 `build/rf-tests/passive-blind-css-multiscale-20261105.json`。

长录制可用 `--scan-all` 分窗扫描。每个核心窗最多一秒，两侧各读至多 250 ms 上下文，同时遵守单窗样本上限。按观测事件起点确定所属窗口，每行输出一个 JSON 对象，包含分析范围、事件归属范围及原录制时间。可结合 `--blind-css`、`--lora-exe` 和时间选择。跨窗重叠有助于完整观察短包，靠近边界且超过上下文的长事件可能只得到部分观测。

有强窄带干扰时可对联合离线扫描追加 `--lora-notch`。工具根据整段样本估计持续谱线，仅在副本上做最多两处窄陷波，并把新增匹配放在 `lora_notch.recovered_events`。原始 LoRa、CSS 和窄带分类仍来自原录制。该选项是实验性诊断，会增加离线处理时间。

宽带突发可用 `--scan --ofdm` 或 `--scan-all --ofdm` 查找 OFDM 循环前缀候选。循环前缀会复制有效符号尾部，可通过固定样本间隔的相关性核对。实现先搜索有代表性的重复间隔，再检查相同前缀位置跨多个符号的相干性以及非前缀位置的对照。输出为 `ofdm_candidates`，含突发时段、有效符号时长、前缀时长和相关指标。当前检查 1/4、1/8、1/16、1/32 四种前缀比例，要求明显的宽带突发，未实现协议解调或连续占用的识别。 [循环前缀与符号结构](https://www.mathworks.com/help/comm/gs/equalization-convolution-and-cyclic-prefix-addition.html)

`python tests/subghz_ofdm_test.py` 在含噪声、频偏和一条延迟回波的合成录制中找到了两段不同参数的循环前缀，正确区分了无前缀的多载波和单音。PSK 回归开启 OFDM 搜索后无误报；三段真实 Pager 录制也各保留一个 LoRa 结果且无 OFDM 候选。

`python tests/subghz_batch_test.py` 在超过单窗上限的录制里验证四段 OOK，其中两段跨窗，四段均各报一次。另将一个真实 BW500/SF7 Pager 波形置于一秒边界，前导码起点 0.997632 秒、SFD 结束 1.001136 秒，联合扫描只输出一个事件。完整 8 秒环境录制可一次执行并输出八行结果。

离线多信道优化：相同宽度的候选共享一次宽带 FFT。每个信道在频域截取并在抽取后的窄带采样率上逆变换，补偿整数 FFT 移频之外的剩余频偏。测试覆盖非整数频点、邻道抑制及相位幅度。Windows 上同一段 2 秒实收背景叠加四信道 FSK，按旧、新、新、旧顺序扫描，平均墙钟耗时约 1.77 秒降到 0.70 秒。两版八条 FSK 的频道、分类和毫秒级起止一致。该测试未包含盲 CSS 或原生 LoRa 的开销，也未验证 ARM 实时处理。原始结果为 `build/rf-tests/subghz-channelizer-ab-20261105.json`。

ARM 上用同类四信道输入，以空闲调度优先级在正式 LoRa 接收旁运行旧、新、新、旧对照。平均子进程 CPU 时间 24.42 → 7.77 秒，墙钟 26.79 → 8.61 秒；两版八条 FSK 的频道、分类及毫秒级时间一致。正式接收期间推进 124583936 对 I/Q，采集与分析计数一致，分析丢样、缺口和结果丢弃均无增量。并行期间 Pager −9 dBm 的 BW500/SF8 中心包检出且录制覆盖预计整帧。新版离线扫描仍慢于这份输入的 2 秒时长，无法据此验收 ARM 连续实时多调制。报告为 `build/rf-tests/subghz-channelizer-arm-20261105.json` 和 `channelizer-background-pager-20261105.json`。

ARM 分项计时后，I/Q 与时频窗采用单精度，背景分位数采用最多 384 帧的分层抽样，实际突发搜索仍逐帧进行。对同一个 2 秒四信道文件做旧、新、新、旧对照，平均 CPU 6.62 → 4.89 秒，墙钟 7.11 → 5.39 秒，峰值 RSS 约 356.3 → 349.1 MiB；八条 FSK 的结果一致。正式 LoRa 的三项丢弃／缺口计数均无增量，最后快照有一个 32768 对样本块待分析。Windows 各类回归和现场背景注入对照通过。报告为 `build/rf-tests/subghz-float32-arm-20261105.json`。实时吞吐与低内存仍未达到目标，下一步可在分块 STFT 中只保留背景样本与累积结果。

2026-11-06：时频谱已改为每次处理 128 帧。背景只计算原分层选中的帧，突发分支只保留峰值、连续计数和前块末两帧状态，盲 CSS 分支只保留每帧脊线位置及质量标志。ARM 以同一份四信道 2 秒输入开启盲 CSS，旧版峰值 RSS 约 349.2 MiB，分块版约 318.7 MiB，最后减少一次频谱复制后的复测约 318.6 MiB。平均 CPU 12.42 → 13.09 秒，减少复制后的单次复测为 12.83 秒。两种分块实现的八条 FSK 及空 CSS 列表与对照一致。Windows 回归及三段真实 Pager 录制的联合检测通过。此改动降低峰值内存，背景帧重复计算带来少量 CPU 开销，ARM 尚未实时。报告为 `build/rf-tests/subghz-block-stft-arm-20261106.json` 与 `subghz-block-stft-final-arm-20261106.json`。

尝试缓存背景功率谱以跳过重复 FFT，但抽取分散帧的数组复制抵消了收益。相同输入上，缓存版 CPU 平均 14.65 秒，原分块版 12.99 秒，缓存试验撤回。随后在 FFT 原生频点顺序中计算，每帧不再搬移半幅频谱，只重排汇总数组和换算峰值索引。两次 ARM 复测平均 CPU 12.23 秒、墙钟 13.29 秒、峰值 RSS 318.6 MiB，八条 FSK 和盲 CSS 输出与对照一致。三段真实 Pager 录制的联合 JSON 也逐项一致。报告为 `build/rf-tests/subghz-stft-cache-arm-20261106.json` 与 `subghz-native-bin-arm-20261106.json`。
