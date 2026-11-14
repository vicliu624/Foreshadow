"""Export a composed, checked application as a portable base archive.

Deterministic package members have Unix epoch timestamps. ZIP requires 1980 or
later, so its exporter supplies fixed ZIP timestamps without changing file bytes.
Raw all-provider installation stages are deliberately not accepted as a base.
"""
import argparse
import gzip
import json
from pathlib import Path
import shutil
import tarfile
import zipfile

from compliance import DIRECTORY, validate


def export(root, output):
    root=root.resolve()
    if output.exists() or output.resolve().is_relative_to(root):
        raise ValueError('Use a new archive outside the application tree')
    inventory=json.loads((root/DIRECTORY/'inventory.json').read_text(encoding='utf-8'))
    manifest=root/'toolbox.json'
    if not inventory.get('package_id') or not manifest.is_file():
        raise ValueError('A composed application package is required; raw stages are not portable bases')
    manifest=json.loads(manifest.read_text(encoding='utf-8'))
    if manifest.get('kind')!='application' or manifest.get('id')!=inventory['package_id']:
        raise ValueError('A composed application package is required')
    validate(root,inventory['package_id'],formal=True)
    output.parent.mkdir(parents=True,exist_ok=True)
    if output.name.endswith('.zip'):
        with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(root.rglob('*')):
                if path.is_symlink():
                    raise ValueError('Use the native bundle exporter for ZIP symlinks')
                if not path.is_file():continue
                info=zipfile.ZipInfo(path.relative_to(root).as_posix(),(1980,1,1,0,0,0))
                info.compress_type=zipfile.ZIP_DEFLATED
                info.external_attr=(path.stat().st_mode & 0xffff)<<16
                with path.open('rb') as source,archive.open(info,'w',force_zip64=True) as target:
                    shutil.copyfileobj(source,target,1024*1024)
    elif output.name.endswith('.tar.gz'):
        with output.open('xb') as file,gzip.GzipFile(filename='',fileobj=file,mode='wb',mtime=0) as compressed:
            with tarfile.open(fileobj=compressed,mode='w|',format=tarfile.PAX_FORMAT) as archive:
                for path in sorted(root.rglob('*')):
                    if path.is_symlink():path.resolve(strict=True).relative_to(root)
                    info=archive.gettarinfo(str(path),arcname=path.relative_to(root).as_posix())
                    info.uid=info.gid=0;info.uname=info.gname='';info.mtime=0
                    if info.isfile():
                        with path.open('rb') as source:archive.addfile(info,source)
                    elif info.isdir() or info.issym():archive.addfile(info)
                    else:raise ValueError('Unsupported portable archive member')
    else:
        raise ValueError('Portable archives must use .zip or .tar.gz')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();export(args.root,args.output)
