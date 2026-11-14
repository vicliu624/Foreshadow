"""Create production corresponding source from a frozen Git commit, never a worktree.

Non-build captures, historic research archives and external test data are omitted
explicitly. Production resources, patched code and build/installation scripts
remain. BUILD_TESTING=OFF is the supported build for this delivery subset.
"""
import argparse
import gzip
import io
import json
from pathlib import Path
import subprocess
import tarfile


def build(repository, commit, output):
    resolved = subprocess.check_output(['git', 'rev-parse', commit + '^{commit}'], cwd=repository, text=True).strip()
    raw = subprocess.check_output(['git', 'archive', '--format=tar', resolved], cwd=repository)
    omitted = []
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('wb') as file, gzip.GzipFile(filename='', fileobj=file, mode='wb', mtime=0) as compressed:
        with tarfile.open(fileobj=compressed, mode='w|', format=tarfile.PAX_FORMAT) as target:
            with tarfile.open(fileobj=io.BytesIO(raw)) as source:
                for info in source:
                    if info.name.startswith(('research/', 'patches/', 'tests/fixtures/', 'firmware/')):
                        omitted.append(info.name)
                        continue
                    info.name = 'foreshadow/' + info.name
                    info.uid = info.gid = 0
                    info.uname = info.gname = ''
                    info.mtime = 0
                    target.addfile(info, source.extractfile(info) if info.isfile() else None)
            data = (json.dumps({'schema': 1, 'source_commit': resolved, 'build_testing': False,
                    'omitted_nonproduction_paths': omitted}, ensure_ascii=False, indent=2) + '\n').encode('utf-8')
            info = tarfile.TarInfo('foreshadow/SOURCE-DELIVERY.json')
            info.size = len(data)
            target.addfile(info, io.BytesIO(data))
    return resolved


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--repository', type=Path, default=Path.cwd())
    p.add_argument('--commit', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(build(a.repository, a.commit, a.output))
