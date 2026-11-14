"""目标机源码接收：比较内容后写入并使用目标机时钟，避免跨机时间差漏编译。"""
import argparse
import hashlib
import json
from pathlib import Path
import tarfile
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--archive', required=True)
parser.add_argument('--root', default='.')
parser.add_argument('--force-refresh', action='store_true')
args = parser.parse_args()
root = Path(args.root).resolve()
allowed = {'src', 'include', 'tests', 'tools', 'docs'}
manifest = {}
changed = []
with tarfile.open(args.archive) as archive:
    for member in archive.getmembers():
        name = Path(member.name)
        if member.isdir():
            continue
        if (not member.isfile() or name.is_absolute() or '..' in name.parts
                or not name.parts or (name.parts[0] not in allowed and str(name) not in {'CMakeLists.txt', 'README.md', '.gitignore'})):
            raise RuntimeError('拒绝归档路径: '+member.name)
        destination = root/name
        if root not in destination.resolve().parents or destination.is_symlink():
            raise RuntimeError('目标路径越界: '+str(destination))
        data = archive.extractfile(member).read()
        if len(data) > 4*1024*1024:
            raise RuntimeError('源码文件超限')
        digest = hashlib.sha256(data).hexdigest()
        manifest[str(name)] = digest
        if not destination.exists() or destination.read_bytes() != data:
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
            changed.append(str(name))
        elif args.force_refresh:
            destination.touch()
            changed.append(str(name))
(root/'source-manifest.json').write_text(json.dumps({'files': manifest, 'applied_unix_time': time.time()}, indent=2), encoding='utf-8')
print(json.dumps({'updated_files': changed}, ensure_ascii=False))
