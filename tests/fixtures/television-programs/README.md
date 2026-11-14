# Synthetic television programs

English · [简体中文](README.zh-CN.md)

`programs.ts` contains a generated 320×180 test pattern at 10 frames/s and two
stereo MP2 sine tracks at 48 kHz. Program 101 includes the 440 Hz and 880 Hz
tracks; program 202 selects the 880 Hz track. Video PID is 256 and audio PIDs
are 257 and 258. The file contains ten video frames and about one second of audio.

The generated fixture and provenance are dedicated to the public domain under
[CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/). There is no recorded
broadcast, third-party music or photographed content. `generate.py` records its
exact stock FFmpeg command, generator version and content hash in
`provenance.json`. Python and an encoder are only needed to regenerate this file;
ordinary tests decode the checked-in stream with the application's native engine.

Tests check actual video, clean PAT/PMT/SDT, PCM spectra for both programs, and
explicit audio-track selection. This is a transport/media test input. It is not
a DTMB-modulated waveform and does not prove synchronization, FEC, RF reception,
AVS+ or DRA support; those require their separate inputs and evidence.
