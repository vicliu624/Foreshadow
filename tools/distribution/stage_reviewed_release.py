"""Transfer publisher-approved formal artifacts; never sign or publish a release."""
import hashlib
import json
import os
import re
import subprocess
import tempfile
import zipfile
from pathlib import Path


def gh(*args, binary=False):
    return subprocess.check_output(['gh', *map(str, args)], text=not binary)


def api(path, method='GET'):
    raw = gh('api', path, '--method', method)
    return json.loads(raw) if raw.strip() else None


def main():
    repo = os.environ['GITHUB_REPOSITORY']
    release_id = os.environ['RELEASE_ID']
    run_id = os.environ['BUILD_RUN_ID']
    source = os.environ['SOURCE_COMMIT']
    approved = os.environ['CHECKSUMS_SHA256']
    assert release_id.isdecimal() and run_id.isdecimal()
    assert re.fullmatch('[0-9a-f]{40}', source) and re.fullmatch('[0-9a-f]{64}', approved)
    endpoint = f'repos/{repo}/releases/{release_id}'
    release = api(endpoint)
    assert release['draft'] and release['tag_name'].startswith('release-inputs-')
    run = api(f'repos/{repo}/actions/runs/{run_id}')
    assert run['status'] == 'completed' and run['conclusion'] == 'success' and run['head_sha'] == source
    assert run['path'] == '.github/workflows/release.yml'
    remote = {a['name']: a for a in release['assets']}
    checksum_asset = remote['SHA256SUMS.txt']
    assert checksum_asset['state'] == 'uploaded' and checksum_asset['digest'] == 'sha256:' + approved
    checksums = gh('api', f'repos/{repo}/releases/assets/{checksum_asset["id"]}', '-H', 'Accept: application/octet-stream', binary=True)
    assert hashlib.sha256(checksums).hexdigest() == approved
    expected = {}
    for line in checksums.decode('utf-8').splitlines():
        digest, name = line.split('  ', 1)
        assert re.fullmatch('[0-9a-f]{64}', digest) and re.fullmatch('[A-Za-z0-9_.-]+', name)
        assert name not in expected
        expected[name] = digest
    expected['SHA256SUMS.txt'] = approved
    assert len(expected) == 22
    names = {'toolboxes-debian13-amd64', 'toolboxes-debian13-arm64', 'toolboxes-Windows-X64',
             'Foreshadow-debian13-amd64', 'Foreshadow-debian13-arm64', 'Foreshadow-windows-x64'}
    artifacts = api(f'repos/{repo}/actions/runs/{run_id}/artifacts?per_page=100')['artifacts']
    artifacts = [a for a in artifacts if a['name'] in names]
    assert len(artifacts) == len(names) and {a['name'] for a in artifacts} == names
    with tempfile.TemporaryDirectory(prefix='foreshadow-reviewed-release-') as temporary:
        root = Path(temporary)
        seen = set()
        for artifact in artifacts:
            assert not artifact['expired']
            archive = root / 'artifact.zip'
            with archive.open('wb') as output:
                subprocess.run(['gh', 'api', f'repos/{repo}/actions/artifacts/{artifact["id"]}/zip'], stdout=output, check=True)
            assert 'sha256:' + hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() == artifact['digest']
            with zipfile.ZipFile(archive) as bundle:
                for member in bundle.infolist():
                    name = member.filename
                    assert name in expected and not member.is_dir() and name not in seen
                    seen.add(name)
                    target = root / name
                    with bundle.open(member) as stream, target.open('wb') as output:
                        while block := stream.read(1024 * 1024):
                            output.write(block)
                    assert hashlib.file_digest(target.open('rb'), 'sha256').hexdigest() == expected[name]
                    # Re-read the private release before every mutation; a published release is never edited.
                    fresh = api(endpoint)
                    assert fresh['draft'] and fresh['tag_name'] == release['tag_name']
                    previous = next((a for a in fresh['assets'] if a['name'] == name), None)
                    if previous and previous['state'] == 'starter':
                        api(f'repos/{repo}/releases/assets/{previous["id"]}', 'DELETE')
                        previous = None
                    if previous:
                        assert previous['state'] == 'uploaded' and previous['digest'] == 'sha256:' + expected[name]
                    else:
                        subprocess.run(['gh', 'release', 'upload', release['tag_name'], str(target), '--repo', repo], check=True)
                    target.unlink()
            archive.unlink()
        fresh = api(endpoint)
        assert fresh['draft']
        uploaded = {a['name']: a for a in fresh['assets']}
        for name, digest in expected.items():
            assert uploaded[name]['state'] == 'uploaded' and uploaded[name]['digest'] == 'sha256:' + digest, name
        print(json.dumps({'status': 'staged-and-verified', 'assets': len(expected), 'source_commit': source, 'published': False}))


if __name__ == '__main__':
    main()
