# 接收机服务与独立基础包

[English](receiver-services.md) · 简体中文

Foreshadow 0.2.12 将基础应用与可选的 SDRplay 接收机客户端分开。基础包保留
RTL-SDR/AIO2、离线分析、监听、录制与研究流程；应用归档不含 SDRplay 客户端及其
专属 SoapySDR 运行库。需要 SDRplay 时安装独立版本的接收机工具箱，连接独立服务。
主程序及自研客户端仍采用 `GPL-3.0-or-later`。

## 为什么选择 SoapyRemote

首先按照现有采集和控制契约评估了 SDRconnect。
官方 [WebSocket 1.0.3 规格](https://sdrplay.com/wp-content/uploads/2026/08/SDRconnect_WebSocket_API.pdf)
定义设备频率、采样率、LNA 状态、天线、过载状态及有符号 16 位交错 I/Q，
但没有记录我们现有提供者的全部设置。硬件采样率本身也不能证明输出 I/Q 的带宽、
缩放和连续性。

| 现有要求 | SDRconnect 公开 WebSocket 契约 | 结论 |
|---|---|---|
| IF 增益衰减及硬件增益模式、回读 | 未完整定义 IFGR 和硬件 AGC；已记录的音频 AGC 是另一项控制 | 无法证明功能等价 |
| 硬件 IF 滤波选择及回读 | 已记录的是 VFO 带宽 | 无法证明功能等价 |
| RF/DAB 陷波、Bias-T、I/Q 校正、HDR、AGC 设定点 | 缺少完整对应属性 | 无法证明功能等价 |
| 原生满量程、完整采集带宽、中断证据 | 定义了有符号 I/Q，但未覆盖所有采集事实 | 需要额外证据 |

接收机工作站没有安装 SDRconnect；获取官方 Linux 安装脚本时返回 HTTP 403。
未执行安装器，也未自动接受厂商条款。这是**公开协议评估，以及原生运行验证的外部阻断**，
不能写成已经实测 SDRconnect 失败。公开控制接口的缺口已经使其不能作为功能等价的替代实现。

因此选用 [SoapyRemote](https://github.com/pothosware/SoapyRemote)，固定为上游
`bd10a2e4a916bf66f0faa36dca7f043d4268138c`（0.5.4），许可证 BSL-1.0。
独立的 `SoapySDRServer` 转发原有 SoapySDR 设备能力、设置及原生 CS16 样本。
客户端只调用组装进模块的远端注册函数，不调用全局设备工厂，也不扫描或加载本地硬件模块。
Windows 厂商 DLL 查找、预加载代码已经移除。

## 安装和使用

1. 安装基础应用。文件、合成评审样本及 RTL-SDR/AIO2 无需接收机服务或 SDRplay 厂商软件。
2. 安装匹配的 **SDRplay Support** 接收机工具箱。0.2.12 及以后需要支持接收机请求协议 2
   的主体。包内包含客户端源码、原始 BSL 声明及构建说明，不含厂商 API、本地 SDRplay 驱动或服务可执行文件。
3. 在连接接收机的电脑上，从 [SDRplay](https://sdrplay.com/hardware-api/) 按厂商条款安装 API，
   另外安装兼容的 MIT SoapySDRPlay3 驱动和 SoapySDRServer。已有厂商安装按原状使用，
   Foreshadow 不代替用户接受厂商条款。
4. 启动服务。本机接收可运行 `SoapySDRServer --bind=127.0.0.1:55132`；
   在可信私网远程使用时，明确绑定接收机电脑的接口地址。SoapyRemote 不提供身份认证或加密，
   不应直接暴露公网。TCP 流还会协商样本及状态端口，只转发 55132 控制端口的 SSH 隧道不足以工作。
5. 打开 **Receiver settings**，选择 **SDRplay**，在 **Receiver service** 输入
   `tcp://主机:55132`，点击 **Refresh devices**。选择设备、序列号、天线、采样率、增益及高级设置，
   点击 **Apply**。服务地址由提供者描述，随该接收机设置保存；应用仍确认延迟的硬件激活并回读参数。
6. 监听、手动录制、波形采集、FM 和电视继续使用原有共享接收会话。停止、释放后其他客户端才能占用设备；
   独立服务不代表允许多个应用同时独占硬件。

无人值守使用可通过操作系统的服务管理器配置外部服务。评审接收机电脑已配置用户服务
`foreshadow-receiver.service`，可用 `systemctl --user status|start|stop|restart
foreshadow-receiver.service` 管理。已配置的评审工作站在接收机设置中保存地址；全新安装时在 Receiver settings 中填写一次地址。基础程序不会静默安装或启动厂商软件。

可选环境变量 `FORESHADOW_SDRPLAY_SERVICE` 提供初始默认值，显式的 `service_endpoint`
设置优先。CLI 示例：

```sh
signalscope devices --receiver sdrplay --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
signalscope probe --receiver sdrplay --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
signalscope configure --receiver sdrplay --rate 2000000 --frequency 478500000 \
  --gain 32 --antenna 'Antenna B' --settings '{"service_endpoint":"tcp://127.0.0.1:55132"}'
```

## 构建与许可边界

应用构建获取 SHA 固定的上游 SoapyRemote 源码，将 BSL 客户端编译进接收机模块。
离线构建提供 `FETCHCONTENT_SOURCE_DIR_SOAPYREMOTE`。`BUILD_TESTING=ON` 时仅为契约测试
构建未修改的上游服务端，不会将其安装进应用或客户端工具箱。独立安装上游服务的方法：

```sh
cmake -S SoapyRemote -B service-build -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_AVAHI=OFF -DCMAKE_INSTALL_PREFIX=/your/service/prefix
cmake --build service-build --config Release --parallel 4
cmake --install service-build --config Release
```

保留服务自身的原始 BSL 许可证和依赖条款。厂商 API 和硬件驱动仍由用户独立安装；
开源适配器不授予厂商软件的再分发权。此前 API 3.15 安装器及 EULA 的证据保留为历史材料，
没有宣称取得 API 或嵌入式固件的新授权。基础包及远端客户端按各自实际依赖闭包检查，
无需继承不相关的本地 API 分发问题。这里以独立可用的服务和网络协议建立边界，
不将进程隔离解释为普遍的许可豁免。

## 协议、资源与连续性

接收机 C ABI 保持 1。请求协议 2 为 `devices`、`probe` 增加可选 `settings` 对象及提供者所有的
`connection_parameters` 元数据，设备枚举前即可渲染。旧回调保留默认行为。主体按通用描述渲染，
通过 CLI、会话和线协议传递设置，不按地址或厂商 ID 路由。新客户端声明最低应用版本 0.2.12。
原命令名、接收机及包 ID、设置根和录制文件继续兼容。使用新服务部署前更新旧的受控 SDRplay
直连工具箱；基础包干净不能证明任意第三方原生模块都安全。

I/Q 保持原生 CS16，并以小端形式录制；字节数和归一化由编解码器负责。TCP 每帧 I/Q 载荷最多
16 KiB，按采样率申请 200 ms 的套接字窗口，限制为 256 KiB–8 MiB。操作系统实际允许的窗口
可能更小，上游服务会报告。缓冲预算不是无丢样保证。

`read_stats.scope=client_transport_reads` 表示客户端读取与超时，不冒充服务端内部驱动读取统计。
已知溢出和突然中断标志使流停止并保留已接受前缀。协议或驱动不提供的硬件丢样计数及服务端驱动读取
次数保持未知；校准和 ADC 过载事实保留原有显式未知状态。连接失败、地址错误、取消和服务重启必须
有限结束并允许恢复，不能虚构连续样本。

## 发布验收

应用组装器按提供者声明的 `bundle_with_application` 及资源、运行时闭包选择内容。可选提供者从
基础归档及其 bundled 清单排除，保留其他提供者仍需要的共享文件。每个独立归档携带各自许可材料闭包。
可选包的未解决问题仍阻断该包，无关基础包可单独通过；通用安装器和签名器不增加厂商分支。

原生契约使用真正的独立上游服务与单独的模拟硬件提供者，覆盖持续输入、短读、溢出、激活失败、恢复、
设置、资源回退和本地驱动误加载陷阱。真实归档还必须通过 Unicode 路径安装、独立工具箱发现、
原生界面及流程、源码和材料机械复验。实机接收和进程模块观察与这些契约分别记录。
本次 Windows/Linux 评审不包含 macOS 验收。

[11 月 14 日 r4 报告](reports/2026-11-14-receiver-service-review.zh-CN.md)记录最终 Windows/Linux
归档、包门禁通过、源码重建、实体服务测试、当前评审入口及接收机电脑的界面截图。
