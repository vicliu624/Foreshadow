"""Fetch pinned validation media; never include these inputs in release assets."""
import argparse
import hashlib
import http.client
from pathlib import Path
import shutil
import ssl
import time
import urllib.error
import urllib.request


REFERENCES = (
    ("avsplus.ts", "https://fate-suite.ffmpeg.org/cavs/avsplus-1080i-testcard.ts",
     "d999a97e1e9a6dcb6859c0194741bac180ae0c11102b145f4fd79b2bdb3f5fdf"),
    ("dra.ts", "https://raw.githubusercontent.com/zuifengjianke/VlcAvsDra/855fb73e70c55355cdc1ccc0374915372cb11630/others/dra.ts",
     "2c6e614c16f1b7f00dad924b7751a28a037a5b9d2107832b912b4f4c22eb7a3c"),
)


def fetch(destination, cache_directory=None):
    destination.mkdir(parents=True, exist_ok=True)
    if cache_directory is not None:
        cache_directory.mkdir(parents=True, exist_ok=True)
    for name, url, expected in REFERENCES:
        target = destination / name
        cached = cache_directory / name if cache_directory is not None else None
        valid = lambda path: path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == expected
        if not valid(target) and cached is not None and valid(cached):
            shutil.copyfile(cached, target)
        if not target.is_file() or hashlib.sha256(target.read_bytes()).hexdigest() != expected:
            temporary = target.with_suffix(".partial")
            for attempt in range(3):
                try:
                    with urllib.request.urlopen(url, timeout=45) as source, temporary.open("wb") as output:
                        total = 0
                        while data := source.read(1024 * 1024):
                            total += len(data)
                            if total > 64 * 1024 * 1024:
                                raise ValueError(f"Independent reference exceeds its download budget: {name}")
                            output.write(data)
                    break
                except (urllib.error.URLError, TimeoutError, ConnectionError, ssl.SSLError, http.client.HTTPException) as error:
                    if isinstance(error, urllib.error.HTTPError) and error.code not in (408, 429) and error.code < 500:
                        raise
                    if attempt == 2:
                        raise
                    print(f"Retrying independent reference {name} after a transfer error ({attempt + 1}/3)", flush=True)
                    time.sleep(1 << attempt)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
                raise ValueError(f"Independent reference checksum mismatch: {name}")
            temporary.replace(target)
        if cached is not None and not valid(cached):
            shutil.copyfile(target, cached)
        print(f"Validated independent media reference: {name} ({target.stat().st_size} bytes)")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--cache-directory", type=Path)
    arguments = parser.parse_args()
    fetch(arguments.destination, arguments.cache_directory)
