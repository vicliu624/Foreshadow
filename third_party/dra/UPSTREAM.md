# DRA decoding core

English · [简体中文](UPSTREAM.zh-CN.md)

Source: <https://github.com/maliwen2015/ffmpeg_cavs_dra>, commit
`eb94ceb887b47b6ecfbda9da43273dd65246df1e`, MIT license in `LICENSE`.
Only the new `libavcodec/dradec.c` and `dradec.h` files were extracted from
`ffmpeg-7.1.2_cavs_dra.patch`; precompiled binaries and the FFmpeg internal
wrapper are not included or used.

SHA-256 before local changes (UTF-8, LF):

- dradec.c: `2d14699330f3cfed6047a24072fc3d8dbfa01e4a345ffc9a8e26be5f2639fca8`
- dradec.h: `84dccc4cf801db80ef7f2a7a206d837bb11cef4b095f20840dc607d6b7347bff`

Local changes: use memmove for overlapping input compaction; avoid subtracting
the sync offset twice; report the actual compressed frame size; interleave all
decoded channels instead of returning a multichannel length for stereo data.
The owning provider independently bounds and frames compressed input, retains
timestamp anchors across PES boundaries, and rejects unsupported auxiliary
profiles explicitly. The decoder executes only in the isolated media helper.
