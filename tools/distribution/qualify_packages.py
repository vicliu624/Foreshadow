"""Reopen the actual release set and qualify the independently composed base.

The output is a fresh Unicode-capable extraction tree for native archive tests
and portable base archives. No signing, installation, vendor setup or publication.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile

from compliance import validate_archive, dump


def qualify(entries_file, output, profiles):
    if output.exists():
        raise ValueError('Use a new release qualification directory')
    output.mkdir(parents=True)
    entries = json.loads(entries_file.read_text(encoding='utf-8'))
    report = {'schema': 1, 'packages': [], 'roots': {}}
    applications = []
    for entry in entries:
        manifest = entry['manifest']
        identity = manifest['id']
        if not identity or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_' for c in identity):
            raise ValueError('Invalid release package identity')
        if identity in report['roots']:
            raise ValueError('Duplicate package in one platform release set')
        archive = entries_file.parent / entry['url'].rsplit('/', 1)[-1]
        with archive.open('rb') as stream:
            actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        if actual != entry['sha256'] or archive.stat().st_size != entry['download_size']:
            raise ValueError('Release archive content differs from its entry')
        evidence = validate_archive(archive, identity, formal=True, trusted_profiles=profiles)
        root = output / identity
        with tarfile.open(archive) as package:
            package.extractall(root, filter='data')
        report['roots'][identity] = str(root.resolve())
        report['packages'].append({'id': identity, 'kind': manifest['kind'], 'sha256': actual, 'verification': evidence})
        if manifest['kind'] == 'application':
            applications.append((root, manifest))
    if len(applications) != 1:
        raise ValueError('One application is required in a platform release set')
    base, application = applications[0]
    data = base / ('Foreshadow.app/Contents/Resources/signalscope' if (base/'Foreshadow.app').exists() else 'share/signalscope')
    bundled = json.loads((data/'distribution/bundled.json').read_text(encoding='utf-8'))
    bundled_ids = {item['id'] for item in bundled}
    for entry in entries:
        manifest = entry['manifest']
        if not manifest.get('bundle_with_application', True):
            if manifest['id'] in bundled_ids:
                raise ValueError('Optional toolbox advertised as bundled in the base')
            for component in manifest['components']:
                if list(base.rglob(Path(component['path']).name)):
                    raise ValueError('Optional native component leaked into the base archive')
    env = os.environ.copy()
    for key in ('SIGNALSCOPE_RECEIVER_PLUGIN_PATH', 'SOAPY_SDR_PLUGIN_PATH', 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH'):
        env.pop(key, None)
    env.update(SIGNALSCOPE_DISTRIBUTION_ROOT=str(output/'isolated-state'), HOME=str(output/'isolated-home'))
    cli = base / application['entrypoints']['cli']
    if os.name == 'nt':
        system = Path(os.environ['SystemRoot'])
        env['PATH'] = os.pathsep.join(map(str, (cli.parent, system/'System32', system)))
    info = json.loads(subprocess.check_output([str(cli), '--build-info'], env=env, timeout=20))
    if info['version'] != application['version']:
        raise ValueError('Extracted base reports another application version')
    plugins = json.loads(subprocess.check_output([str(cli), 'receiver-plugins'], env=env, timeout=20))
    expected = {c['id'] for item in bundled if item['kind']=='receiver' for c in item['components']}
    loaded = {p['id'] for p in plugins if p['status']=='loaded'}
    if loaded != expected or any(p['status']=='rejected' for p in plugins):
        raise ValueError('Extracted base runtime differs from the declared bundled receivers')
    report.update(status='passed', base_root=str(base.resolve()), build_info=info, base_receivers=sorted(loaded))
    dump(output/'qualification.json', report)
    return report


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--entries', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--profiles', type=Path, default=Path(__file__).resolve().parents[2]/'resources/licensing/profiles')
    args = p.parse_args()
    result = qualify(args.entries, args.output, args.profiles)
    print(json.dumps({'status': result['status'], 'base_root': result['base_root'], 'roots': result['roots']}))
