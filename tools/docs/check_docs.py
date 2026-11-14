#!/usr/bin/env python3
"""Check the bilingual documentation inventory and repository-local links."""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import unicodedata
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[2]


def markdown_body(text: str) -> str:
    return re.sub(r"(?ms)^\s*(`{3,}|~{3,}).*?^\s*\1\s*$", "", text)


def anchors(text: str) -> set[str]:
    result: set[str] = set()
    counts: dict[str, int] = {}
    for match in re.finditer(r"(?m)^#{1,6}\s+(.+?)\s*#*\s*$", markdown_body(text)):
        title = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", match[1])
        title = re.sub(r"<[^>]+>", "", title).strip().lower()
        title = "".join(c for c in title if c in " -_" or unicodedata.category(c)[0] in "LN")
        slug = title.replace(" ", "-")
        count = counts.get(slug, 0)
        counts[slug] = count + 1
        result.add(slug + (f"-{count}" if count else ""))
    result.update(re.findall(r'<a\s+(?:name|id)=[\"\']([^\"\']+)', text))
    return result


def check(root: Path) -> list[str]:
    errors: list[str] = []
    manifest = json.loads((root / "docs/documents.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or manifest.get("default_language") != "en":
        errors.append("documents.json must use schema 1 and default_language en")
    indexed: set[str] = set()
    contents: dict[Path, str] = {}
    for pair in manifest["documents"]:
        if pair.get("status") not in {"current", "historical", "decision", "fixture"}:
            errors.append(f"Invalid status: {pair}")
        for locale, other in (("en", "zh-CN"), ("zh-CN", "en")):
            name = pair[locale]
            if name in indexed:
                errors.append(f"Duplicate inventory path: {name}")
            indexed.add(name)
            path = root / name
            if not path.is_file():
                errors.append(f"Missing {locale} counterpart: {name}")
                continue
            text = path.read_text(encoding="utf-8-sig")
            contents[path.resolve()] = text
            if len(text.strip()) < 100:
                errors.append(f"Empty/incomplete document: {name}")
            if not re.search(r"(?m)^# .+", text):
                errors.append(f"Missing title: {name}")
            if locale == "en":
                heading = re.search(r"(?m)^# (.+)", text)
                if heading and re.search(r"[\u4e00-\u9fff]", heading[1]):
                    errors.append(f"Default title is not English: {name}")
            destinations = [unquote(m[1].strip("<>")) for m in re.finditer(r"\[[^\]]*\]\(([^)]+)\)", text[:1500])]
            counterpart = (root / pair[other]).resolve()
            if not any((path.parent / x).resolve() == counterpart for x in destinations if not urlsplit(x).scheme):
                errors.append(f"Missing language navigation: {name}")
    tracked = subprocess.check_output(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"], cwd=root
    ).decode("utf-8").split("\0")
    actual = {name for name in tracked if name.lower().endswith(".md")}
    errors.extend(f"Markdown missing from bilingual inventory: {name}" for name in sorted(actual - indexed))
    errors.extend(f"Inventory path is ignored/untracked: {name}" for name in sorted(indexed - actual) if (root / name).is_file())
    for path, text in contents.items():
        for match in re.finditer(r"!?\[[^\]]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)", markdown_body(text)):
            destination = match[1].strip("<>")
            parts = urlsplit(destination)
            if parts.scheme or parts.netloc:
                continue
            relative = unquote(parts.path)
            target = (root / relative.lstrip("/") if relative.startswith("/") else path.parent / relative).resolve() if relative else path
            label = f"{path.relative_to(root)}: {destination}"
            if not target.is_relative_to(root.resolve()):
                errors.append(f"Link leaves repository: {label}")
            elif not target.exists():
                errors.append(f"Broken local link: {label}")
            elif parts.fragment and target.suffix.lower() == ".md":
                body = contents.get(target)
                if body is None:
                    body = target.read_text(encoding="utf-8-sig")
                if unquote(parts.fragment).lower() not in anchors(body):
                    errors.append(f"Missing heading anchor: {label}")
            if match[0].startswith("![") and target.is_file() and target.suffix.lower() == ".png":
                header = target.read_bytes()[:24]
                if len(header) != 24 or header[:8] != b"\x89PNG\r\n\x1a\n" or not all(struct.unpack(">II", header[16:24])):
                    errors.append(f"Invalid PNG: {label}")
    return errors


def main() -> int:
    # The CLI is a UTF-8 boundary, including diagnostics for non-BMP paths.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    args = parser.parse_args()
    errors = check(args.root.resolve())
    for error in errors:
        print(error)
    if errors:
        print(f"Documentation check failed: {len(errors)} issue(s)")
        return 1
    manifest = json.loads((args.root / "docs/documents.json").read_text(encoding="utf-8"))
    print(f"Documentation check passed: {len(manifest['documents'])} bilingual pairs; local links and PNGs valid")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
