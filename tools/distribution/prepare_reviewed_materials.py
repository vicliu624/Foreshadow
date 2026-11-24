"""Reuse only profile-pinned dependency source from an earlier public release.

First-party source and repository notices always come from the current frozen
commit. This formal-release staging job never signs or publishes anything.
"""
import hashlib
import json
import os
import re
import subprocess
import tarfile
import tempfile
from pathlib import Path
from collect_materials import collect
from make_source_bundle import build


def gh(*args):
    return subprocess.check_output(['gh', *map(str, args)], text=True)


def main():
    repo = os.environ['GITHUB_REPOSITORY']
    release_id = os.environ['RELEASE_ID']
    base_tag = os.environ['BASE_TAG']
    assert release_id.isdecimal() and re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+', base_tag)
    endpoint = f'repos/{repo}/releases/{release_id}'
    release = json.loads(gh('api', endpoint))
    assert release['draft'] and release['tag_name'].startswith('release-inputs-')
    root = Path(__file__).resolve().parents[2]
    source = subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True).strip()
    assert source == os.environ['GITHUB_SHA']
    profiles = sorted((root / 'resources/licensing/profiles').glob('*.json'))
    version = json.loads(profiles[0].read_text(encoding='utf-8'))['components']['foreshadow']['version']
    assert release['tag_name'] == 'release-inputs-' + version
    expected = {}
    for profile in profiles:
        for component in json.loads(profile.read_text(encoding='utf-8'))['components'].values():
            for artifact in component.get('artifacts', []):
                if artifact.get('first_party'):
                    continue
                key = artifact['file']
                assert Path(key).name == key and expected.get(key, artifact['sha256']) == artifact['sha256']
                expected[key] = artifact['sha256']
    old = json.loads(gh('api', f'repos/{repo}/releases/tags/{base_tag}'))
    assert not old['draft'] and not old['prerelease']
    candidates = [a for a in old['assets'] if a['name'].endswith('.amcpkg') or
                  (a['name'].startswith('org.amc.receiver.sdrplay-') and a['name'].endswith('.amctbx'))]
    assert len(candidates) == 6
    with tempfile.TemporaryDirectory(prefix='foreshadow-materials-') as temporary:
        work = Path(temporary)
        cache = work / 'cache'
        cache.mkdir()
        for asset in candidates:
            archive = work / 'previous-package'
            with archive.open('wb') as output:
                subprocess.run(['gh', 'api', f'repos/{repo}/releases/assets/{asset["id"]}',
                                '-H', 'Accept: application/octet-stream'], stdout=output, check=True)
            assert asset['state'] == 'uploaded' and asset['digest'] == 'sha256:' + hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
            # Read named, hash-pinned regular files only; never extract old paths or execute old package code.
            with tarfile.open(archive) as bundle:
                for member in bundle:
                    name = Path(member.name).name
                    if name not in expected or not member.isfile() or (cache / name).exists():
                        continue
                    data = bundle.extractfile(member).read()
                    assert hashlib.sha256(data).hexdigest() == expected[name], name
                    (cache / name).write_bytes(data)
            archive.unlink()
        assert {p.name for p in cache.iterdir()} == set(expected), 'Some pinned dependency source is absent'
        first_party = work / f'foreshadow-{version}-source.tar.gz'
        build(root, source, first_party)
        materials = work / 'materials'
        for profile in profiles:
            collect(profile, cache, root, first_party, source, materials / profile.stem)
        bundle = work / f'reviewed-compliance-{version}.tar.gz'
        with tarfile.open(bundle, 'w:gz', compresslevel=1) as stream:
            for platform in sorted(materials.iterdir()):
                stream.add(platform, arcname=platform.name)
        record = {'schema': 1, 'source_commit': source, 'version': version, 'base_tag': base_tag,
                  'sha256': hashlib.file_digest(bundle.open('rb'), 'sha256').hexdigest(), 'size': bundle.stat().st_size,
                  'path': bundle.name, 'first_party_source_sha256': hashlib.file_digest(first_party.open('rb'), 'sha256').hexdigest(),
                  'dependency_artifacts': expected, 'published': False, 'signing_keys': 'not accessed'}
        receipt = work / 'bundle-input.json'
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        fresh = json.loads(gh('api', endpoint))
        assert fresh['draft'] and fresh['tag_name'] == release['tag_name']
        subprocess.run(['gh', 'release', 'upload', release['tag_name'], str(bundle), str(first_party), str(receipt), '--repo', repo], check=True)
        print(json.dumps({'status': 'reviewed-materials-staged', 'source_commit': source, 'version': version, 'published': False}))


if __name__ == '__main__':
    main()
