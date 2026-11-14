# 独立离线 DTMB 波形

[English](README.md) · 简体中文

生成器从 `../television-programs` 可再分发的合成双节目 TS 出发，执行扰码、BCH/LDPC、星座映射、卷积交织、系统信息、C3780频率交织/逆 FFT（C1直接输出符号）、PN 帧头和独立 SRRC 发射滤波，只写文件，不连接或驱动无线电发射。

`gb20600-tables.json` 数值提取自 [GB 20600-2006](https://upload.wikimedia.org/wikipedia/commons/2/2c/GB_20600%E2%80%942006.pdf)，PDF SHA-256 为 `3dd373d098e0204ef17b77384f8685a4026f9ddc09522d7832a981267a4dca8f`：附录A生成多项式、B稀疏校验循环矩阵、C的NR映射、D/E的PN相位向量、F频率置换、G系统信息向量。不含扫描图像或广播媒体。提取检查维度、索引、唯一性与表完整性。用 `extract_tables.py PDF` 复现；只有该维护命令需要 PyMuPDF。输出哈希：`f200fad70877d1de79450b8eaa388fdc47c9bb88fad3211c8f07f132b95d6013`。

127bit十六进制生成值按多项式系数加载，串行坐标明确从低到高，循环矩阵行向右移。`dtmb_fec_finite_tail` 对每个信息块和循环边界，依据独立提取的附录B矩阵验证编码器，再通过实际原生解码器检查全部三种码率，防止错误反转生成表示或仅靠接收器自洽往返充当真值。完整波形还将一个 FFT 卷积码字与精确整数 XOR/旋转运算比较。Python 发射器不导入接收实现。

录制开始前发射交织器已预热，接收解交织器仍从空状态捕获并丢弃真实延迟。不能把未预热发射器的补零启动当作稳态广播，这会产生无效功率/归一化条件。载荷比较逐一检查全部原始非空媒体包及顺序，不仅检查可辨认节目名。

PN595遵循4.6.2.2节图12：D1…D9=0，D10=1，第一次时钟前输出D10，前缀 `80249A6B`。PN420/945支持完整已公布相位计划；默认相位0也是不要求帧编号时标准允许的选择。`--phase-cycle` 覆盖相位计划。

在仓库根目录执行（Python3/NumPy只用于离线生成及资格检查）：

```sh
python3 tests/fixtures/dtmb-waveform/generate.py \
  --pn 945 --si 22 --frames 1800 \
  --output '/tmp/电视验收 📡/programs.sigmf-data'
python3 tests/fixtures/dtmb-waveform/verify.py \
  --decoder /path/to/build/dtmb_receiver_test \
  --output /path/to/qualification --cases all --phase-cycle
```

同时输出 SigMF 元数据、生成事实和源 TS。默认 CF32LE、15.12MS/s、SRRC alpha0.05、每符号两样本、64符号滤波跨度。`--symbol-spaced` 是脉冲成形前的诊断输入，不能验收完整 RF 链。

`verify.py --cases all` 检查66种声明的 C3780 PN/配置组合和6种 PN595/4QAM C1 配置，记录实际发现模式、节目ID、逐包精确比较、CRC、连续性、FEC syndrome/BCH及时序事实。失败或不完整案例使命令失败，不会静默跳过。除 `--keep-iq` 外只删除命令新建用例目录内的 I/Q，保留 JSON 证明与解码日志。常规 CTest 不依赖 Python/NumPy。

这是干净合成输入的接收链验收，不证明桌面呈现、音频设备、实时吞吐、变化时钟、多径表现、AVS+/DRA或当地天线实收。生成媒体按 `../television-programs` 的 CC0 声明处理；生成代码遵从项目许可状态。外部实录参考仍为独立正例，不能用该合成样本替换。

原矩阵保持配置的 SI 向量不变。`generate.py` 或 `verify.py` 添加 `--superframe-markers`，可将每个首超帧 SI 替换为规范奇/偶向量1/2。这与 PN 帧头相位循环分开，均不改变载荷调制/码率。流中配置切换和保留 SI 需要单独验收。

`verify_formats.py --decoder BUILD/dtmb_receiver_test --output DIRECTORY` 另检查10MS/s的完整 PN945/SI22 波形、首超帧标记及 CI16LE/CU8。使用独立 SciPy `resample_poly(125,189)`，显式量化 CI16 scale32768、CU8 scale128/midpoint127.5。只有离线检查需要 SciPy。SigMF与证明包含哈希/削顶计数，不隐含实体接收结论。
