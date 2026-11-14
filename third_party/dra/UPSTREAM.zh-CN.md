# DRA 解码核心来源

[English](UPSTREAM.md) · 简体中文

来源：[maliwen2015/ffmpeg_cavs_dra](https://github.com/maliwen2015/ffmpeg_cavs_dra)，提交 `eb94ceb887b47b6ecfbda9da43273dd65246df1e`，MIT 许可见 `LICENSE`。只从 `ffmpeg-7.1.2_cavs_dra.patch` 提取新增的 `libavcodec/dradec.c` 与 `dradec.h`，没有包含或使用预编译二进制及 FFmpeg 内部包装层。

本地修改前的 SHA-256（UTF-8、LF）：

- dradec.c：`2d14699330f3cfed6047a24072fc3d8dbfa01e4a345ffc9a8e26be5f2639fca8`
- dradec.h：`84dccc4cf801db80ef7f2a7a206d837bb11cef4b095f20840dc607d6b7347bff`

本地修改：输入压缩整理存在重叠时使用 memmove；避免重复减去同步偏移；返回实际压缩帧大小；交织所有已解码声道，避免只提供立体声数据却返回多声道长度。

所属提供者独立约束压缩输入大小与分帧，在 PES 边界之间保留时间戳锚点，明确拒绝不支持的辅助配置。解码只在隔离的媒体辅助进程中执行。
