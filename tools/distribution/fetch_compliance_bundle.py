"""Fetch an explicitly reviewed material bundle for formal release assembly.

The caller supplies its pinned SHA256. This is not a permission/clearance flag;
package composition and signing still check profile, versions and conditions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request


def verify_source_commits(materials, expected):
    records = sorted(materials.glob('*/materials.json'))
    if not records:
        raise ValueError('Reviewed bundle has no platform material inventories')
    for path in records:
        value = json.loads(path.read_text(encoding='utf-8'))
        if value.get('source_commit') != expected:
            raise ValueError(f'Reviewed materials source commit mismatch: {path.parent.name}')


def fetch(url, expected, destination, source_commit=None):
    if not url.startswith('https://') or len(expected) != 64 or any(c not in '0123456789abcdef' for c in expected):
        raise ValueError('A reviewed HTTPS bundle and lowercase SHA256 are required')
    if destination.exists():
        raise ValueError('Use a fresh material bundle destination')
    destination.mkdir(parents=True)
    archive = destination / 'reviewed-materials.tar.gz'
    with urllib.request.urlopen(url, timeout=60) as response, archive.open('wb') as output:
        while chunk := response.read(1024 * 1024):
            output.write(chunk)
    with archive.open('rb') as stream:
        actual = hashlib.file_digest(stream, 'sha256').hexdigest()
    if actual != expected:
        raise ValueError('Reviewed compliance bundle checksum mismatch')
    with tarfile.open(archive) as stream:
        stream.extractall(destination / 'materials', filter='data')
    if source_commit:
        verify_source_commits(destination / 'materials', source_commit)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--url', required=True)
    p.add_argument('--sha256', required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--source-commit', help='Require every platform inventory to match this frozen commit')
    a = p.parse_args()
    fetch(a.url, a.sha256, a.output, a.source_commit)
