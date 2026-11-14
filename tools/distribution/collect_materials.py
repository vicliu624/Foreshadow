"""Materialize a reviewed data-only dependency profile into portable evidence.

Cache inputs are SHA256 pinned in the profile. The collector never guesses a
license from a package name and never downloads, accepts vendor terms or installs
anything. New providers supply declarations, profiles and artifacts as resources.
"""
import argparse
import fnmatch
import io
import json
import hashlib
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import zipfile

from compliance import check_components, digest, dump, load


def archive_files(path):
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as stream:
            for info in stream.infolist():
                if not info.is_dir():
                    yield info.filename, lambda i=info: stream.read(i)
    else:
        with tarfile.open(path, 'r:*') as stream:
            for info in stream:
                if info.isfile():
                    yield info.name, lambda i=info: stream.extractfile(i).read()


def collect(profile_path, cache, repository, first_party, commit, output):
    profile = load(profile_path)
    reviewed = json.loads(json.dumps(profile))
    if output.exists():
        raise ValueError('Use a new materials directory')
    output.mkdir(parents=True)
    profile['source_commit'] = commit
    components = profile['components']
    for name, component in components.items():
        component['id'] = name
        component['materials'] = []
        def copy(source, relative, role):
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
            component['materials'].append({'path': relative, 'role': role, 'sha256': digest(target)})
        for artifact in component.pop('artifacts', []):
            if artifact.get('first_party'):
                source = first_party
                component['source_commit'] = commit
                # The delivery manifest binds the archive to the frozen source.
                with tarfile.open(source) as stream:
                    delivered = json.load(stream.extractfile('foreshadow/SOURCE-DELIVERY.json'))
                if delivered['source_commit'] != commit:
                    raise ValueError('First-party source archive has another commit')
            else:
                source = cache / artifact['file']
                if digest(source) != artifact['sha256']:
                    raise ValueError(f'Cached source checksum mismatch: {source}')
            role = artifact.get('role', 'source')
            folder = {'source': 'source', 'notice': 'notices', 'build': 'build'}.get(role, 'metadata')
            relative = name + '/' + folder + '/' + source.name
            copy(source, relative, role)
            if artifact.get('extract_notices'):
                for member, read in archive_files(source):
                    pure = PurePosixPath(member)
                    if pure.is_absolute() or '..' in pure.parts or '\\' in member:
                        raise ValueError('Unsafe source archive member')
                    basename = pure.name.lower()
                    patterns = artifact.get('notice_patterns', ['license*', 'copying*', 'copyright*', 'notice*', 'authors*',
                                                               'qt_attribution.json', '*.license', '*.spdx', 'ftl.txt', 'bdflicense.txt'])
                    license_directory = any(part.lower() in ('licenses', 'licences') for part in pure.parts)
                    if license_directory or any(fnmatch.fnmatchcase(basename, p) for p in patterns):
                        # Upstream paths can be hundreds of characters long.
                        # Keep the original location as data, without making a
                        # Windows install transaction recreate that hierarchy.
                        key = hashlib.sha256((source.name + '\0' + member).encode('utf-8')).hexdigest()[:16]
                        destination = name + '/notices/' + key + '-' + pure.name[-48:]
                        target = output / destination
                        target.parent.mkdir(parents=True, exist_ok=True)
                        if target.exists():
                            raise ValueError('Duplicate generated notice path')
                        target.write_bytes(read())
                        component['materials'].append({'path': destination, 'role': 'notice', 'sha256': digest(target),
                                                       'origin_archive': source.name, 'origin_member': member})
        for item in component.pop('repository_files', []):
            if item.get('sha256') != digest(repository / item['file']):
                raise ValueError(f'Repository material differs from reviewed profile: {item["file"]}')
            copy(repository / item['file'], name + '/' + item['file'], item['role'])
        # Profile-specific versions, selected alternative licenses, SDK flags,
        # upstream origins and unresolved conditions travel as a notice too.
        identity = output / name / 'COMPONENT.json'
        dump(identity, {k: v for k, v in component.items() if k != 'materials'})
        component['materials'].append({'path': identity.relative_to(output).as_posix(), 'role': 'metadata', 'sha256': digest(identity)})
    for file in ('LICENSE', 'LICENSING.md', 'LICENSING.zh-CN.md', 'THIRD_PARTY_NOTICES.md',
                 'THIRD_PARTY_NOTICES.zh-CN.md', 'SOURCE_AND_REPLACEMENT.md', 'SOURCE_AND_REPLACEMENT.zh-CN.md'):
        shutil.copy2(repository / file, output / file)
    check_components(output, components)
    dump(output / 'reviewed-profile.json', reviewed)
    profile['profile_sha256'] = digest(output / 'reviewed-profile.json')
    dump(output / 'materials.json', profile)
    return len(components)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--profile', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--repository', type=Path, default=Path.cwd())
    p.add_argument('--first-party-source', type=Path, required=True)
    p.add_argument('--source-commit', required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    print(collect(a.profile, a.cache, a.repository, a.first_party_source, a.source_commit, a.output))
