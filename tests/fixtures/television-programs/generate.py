"""Regenerate the synthetic, redistributable media fixture with stock FFmpeg.

This generator is independent of the application's receiver/media implementation.
Python and an encoder are only needed to regenerate the checked-in fixture.
"""
from pathlib import Path
import hashlib
import json
import subprocess

directory = Path(__file__).resolve().parent
arguments = [
    "ffmpeg", "-hide_banner", "-loglevel", "error",
    "-f", "lavfi", "-i", "testsrc2=size=320x180:rate=10:duration=1",
    "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=1",
    "-f", "lavfi", "-i", "sine=frequency=880:sample_rate=48000:duration=1",
    "-map", "0:v", "-map", "1:a", "-map", "2:a",
    "-c:v", "mpeg2video", "-g", "5", "-bf", "0", "-b:v", "350k",
    "-c:a", "mp2", "-b:a", "64k", "-ar", "48000", "-ac", "2",
    "-program", "program_num=101:st=0:st=1:st=2",
    "-program", "program_num=202:st=0:st=2",
    "-metadata:p:0", "service_name=AMC-440", "-metadata:p:1", "service_name=AMC-880",
    "-f", "mpegts", "-y", "programs.ts",
]
subprocess.run(arguments, cwd=directory, check=True)
provenance = {
    "origin": "Synthetic FFmpeg testsrc2 pattern and 440/880 Hz sine tones; no broadcast content",
    "generator": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
    "arguments": arguments,
    "sha256": hashlib.sha256((directory / "programs.ts").read_bytes()).hexdigest(),
    "video_pid": 256, "audio_pids": [257, 258], "programs": [101, 202],
    "license": "CC0-1.0 for the generated fixture and its provenance",
    "scope": "Media/TS playback and selection only; this is not a DTMB-modulated waveform",
}
(directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
print(provenance["sha256"])
