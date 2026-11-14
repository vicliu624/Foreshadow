# 合成电视节目

[English](README.md) · 简体中文

`programs.ts` 包含生成的320×180、10帧/秒测试图及两条48kHz立体声 MP2 正弦音轨。节目101包含440Hz与880Hz音轨，节目202选择880Hz。视频 PID256，音频 PID257、258，共十帧视频和约一秒音频。

生成样本及来源记录依照 [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/) 贡献至公共领域，不含广播录音、第三方音乐或摄影内容。`generate.py` 在 `provenance.json` 保存准确的原版 FFmpeg 命令、生成器版本和内容哈希。只有重新生成才需要 Python 与编码器；常规测试使用应用原生引擎解码已入库的流。

测试验证实际视频、无错误 PAT/PMT/SDT、两个节目的 PCM 频谱和显式音轨选择。这是传输流/媒体测试输入，不是 DTMB 调制波形，不能证明同步、FEC、射频接收、AVS+ 或 DRA 支持；这些需要各自的输入和证据。
