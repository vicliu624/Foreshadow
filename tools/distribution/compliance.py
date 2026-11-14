"""Version 1 package compliance protocol. Providers own declarations and materials.

This collector checks evidence and content, not the legal accuracy of a grant.
Only the release assembly layer calls it; installers and UI do not know providers.
"""
from pathlib import Path, PurePosixPath
import fnmatch
import hashlib
import json
import shutil
import tarfile
import html


DIRECTORY = 'share/foreshadow/compliance'


def dump(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def relative(root, name):
    """A material is a regular file inside its bundle, never a link or traversal."""
    pure = PurePosixPath(name)
    if not name or '\\' in name or pure.is_absolute() or any(x in ('', '.', '..') for x in name.split('/')):
        raise ValueError(f'Invalid compliance path: {name}')
    path = root.joinpath(*pure.parts)
    if path.is_symlink() or not path.is_file():
        raise ValueError(f'Missing regular compliance material: {name}')
    path.resolve().relative_to(root.resolve())
    return path


def binary(path):
    if path.is_symlink() or not path.is_file():
        return False
    with path.open('rb') as stream:
        magic = stream.read(4)
    return magic[:2] == b'MZ' or magic == b'\x7fELF' or magic in (
        b'\xcf\xfa\xed\xfe', b'\xfe\xed\xfa\xcf', b'\xca\xfe\xba\xbe')


def load(path):
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('schema') != 1:
        raise ValueError(f'Unsupported compliance schema: {path}')
    return value


def render_index(inventory):
    """English default legal document; complete original notices remain files."""
    rows = []
    notices = []
    for name, component in sorted(inventory['components'].items()):
        if component.get('display_notice'):
            notices.append('<p>' + html.escape(component['display_notice']) + '</p>')
        links = ' · '.join('<a href="' + html.escape(item['path'], quote=True) + '">' +
                           html.escape(item['role'] + ': ' + Path(item['path']).name) + '</a>'
                           for item in component['materials'])
        issues = '<br>'.join(html.escape(x) for x in component['issues'])
        rows.append('<tr><td>' + html.escape(name) + '</td><td>' + html.escape(component['version']) +
                    '</td><td>' + html.escape(component['license']) + '</td><td>' + links + '</td><td>' + issues + '</td></tr>')
    return ('<!doctype html><html lang="en"><meta charset="utf-8"><title>Foreshadow — Licenses and source</title>'
        '<style>body{background-color:#142033;color:#dce7f5;font-family:sans-serif}'
        'a{color:#5bdcc6}td,th{vertical-align:top}</style>'
        '<body><h1>Foreshadow — Licenses and source</h1>' + ''.join(notices) +
        '<p>Source commit: <code>' + html.escape(inventory['source_commit']) + '</code></p>'
        '<p><a href="LICENSING.md">Licensing scope</a> · <a href="LICENSING.zh-CN.md">Licensing scope (Chinese)</a> · '
        '<a href="SOURCE_AND_REPLACEMENT.md">Build and replace libraries</a> · '
        '<a href="SOURCE_AND_REPLACEMENT.zh-CN.md">Build and replace libraries (Chinese)</a></p>'
        '<table border="1" bordercolor="#49647f" cellpadding="6"><tr><th>Component</th><th>Version</th><th>License</th>'
        '<th>Original notices, corresponding source and build material</th><th>Unresolved conditions</th></tr>' +
        ''.join(rows) + '</table><p>Materials accompany this copy. Evidence completeness is a mechanical check; '
        'unresolved conditions prevent official distribution.</p></body></html>')


def index(directory, inventory):
    (directory / 'index.html').write_text(render_index(inventory), encoding='utf-8', newline='\n')


def closure(components, roots):
    selected = set()
    def visit(name):
        if name in selected:
            return
        if name not in components:
            raise ValueError(f'Missing component evidence: {name}')
        selected.add(name)
        for dependency in components[name].get('dependencies', []):
            visit(dependency)
    for name in roots:
        visit(name)
    return selected


def check_components(root, components):
    for name, component in components.items():
        if component.get('id') != name or not all(component.get(k) for k in ('version', 'license', 'origin')):
            raise ValueError(f'Incomplete component identity: {name}')
        files = component.get('materials', [])
        roles = {item['role'] for item in files}
        required = {'notice'}
        if component.get('source_required', True):
            required |= {'source', 'build'}
        if not required <= roles:
            raise ValueError(f'Missing {required - roles} material for {name}')
        for item in files:
            path = relative(root, item['path'])
            if not item.get('sha256') or digest(path) != item['sha256']:
                raise ValueError(f'Material checksum mismatch: {name}/{item["path"]}')
        if not isinstance(component.get('issues'), list):
            raise ValueError(f'Explicit review status required: {name}')
    closure(components, components)


def prepare(stage, materials, declarations, facts, commit):
    """Attach verified external evidence to a CMake installation before packaging."""
    evidence = load(materials / 'materials.json')
    if evidence.get('platform') != facts['platform']:
        raise ValueError('Material platform does not match the installed build')
    components = evidence['components']
    check_components(materials, components)
    packages = {}
    application = []
    for path in sorted(declarations.glob('*.json')):
        provider = load(path)
        for package, roots in provider['packages'].items():
            packages.setdefault(package, []).extend(roots)
        application.extend(provider.get('application', []))
    if not packages or not application:
        raise ValueError('No provider compliance declarations installed')
    for name, expected in evidence.get('build_facts', {}).items():
        if facts.get(name) != expected:
            raise ValueError(f'Build/source version mismatch for {name}: {facts.get(name)!r} != {expected!r}')
    app_id = evidence['application_id']
    packages.setdefault(app_id, []).extend(application)
    for package, roots in packages.items():
        packages[package] = sorted(set(roots))
    if 'packages' in evidence and packages != evidence['packages']:
        raise ValueError('Provider declarations differ from reviewed package roots')
    for roots in packages.values():
        closure(components, roots)
    destination = stage / DIRECTORY
    if destination.exists():
        raise ValueError('Compliance destination already exists; use a fresh stage')
    destination.mkdir(parents=True)
    all_files = {item['path'] for c in components.values() for item in c['materials']}
    for name in sorted(all_files):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(relative(materials, name), target)
    for path in sorted(materials.glob('*.md')):
        shutil.copy2(path, destination / path.name)
    if (materials / 'LICENSE').is_file():
        shutil.copy2(materials / 'LICENSE', destination / 'LICENSE')
    if 'profile_sha256' in evidence:
        shutil.copy2(materials / 'reviewed-profile.json', destination / 'reviewed-profile.json')
    value = {'schema': 1, 'platform': facts['platform'], 'source_commit': commit,
             'build_facts': facts, 'components': components, 'packages': packages,
             'runtime_rules': evidence['runtime_rules']}
    if 'profile_sha256' in evidence:
        value['profile_sha256'] = evidence['profile_sha256']
    dump(destination / 'inventory.json', value)
    index(destination, value)
    # The aggregate is also checked as a real package, without accepting an
    # unowned executable that happened to be present in the build machine.
    result = validate(stage, app_id, formal=False, check_all=True)
    value['runtime_fingerprints'] = [dict(component=b['component'], name=Path(b['path']).name, sha256=b['sha256'])
                                     for b in result['binaries']]
    dump(destination / 'inventory.json', value)


def attach(stage, package_root, package_id, commit, formal):
    origin = stage / DIRECTORY
    inventory = load(origin / 'inventory.json')
    if inventory['source_commit'] != commit:
        raise ValueError('Corresponding-source commit differs from package commit')
    selected = closure(inventory['components'], inventory['packages'].get(package_id, []))
    if not selected:
        raise ValueError(f'Package has no compliance declaration: {package_id}')
    components = {name: inventory['components'][name] for name in sorted(selected)}
    destination = package_root / DIRECTORY
    if destination.exists():
        # Application composition already copied the aggregate; replace only
        # this owned subtree with the selected package's evidence.
        destination.resolve().relative_to(package_root.resolve())
        shutil.rmtree(destination)
    destination.mkdir(parents=True)
    for name in sorted({m['path'] for c in components.values() for m in c['materials']}):
        target = destination / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(relative(origin, name), target)
    for path in sorted(origin.glob('*.md')):
        shutil.copy2(path, destination / path.name)
    if (origin / 'LICENSE').is_file():
        shutil.copy2(origin / 'LICENSE', destination / 'LICENSE')
    if 'profile_sha256' in inventory:
        shutil.copy2(origin / 'reviewed-profile.json', destination / 'reviewed-profile.json')
    inventory['components'] = components
    inventory['package_id'] = package_id
    inventory['packages'] = {package_id: inventory['packages'][package_id]}
    inventory['runtime_rules'] = [r for r in inventory['runtime_rules'] if r['component'] in selected]
    dump(destination / 'inventory.json', inventory)
    index(destination, inventory)
    result = validate(package_root, package_id, formal=formal)
    dump(destination / 'verification.json', result)
    return result


def validate(root, package_id, formal=True, check_all=False):
    directory = root / DIRECTORY
    inventory = load(directory / 'inventory.json')
    if package_id not in inventory['packages']:
        raise ValueError(f'Package missing from compliance inventory: {package_id}')
    components = inventory['components']
    check_components(directory, components)
    if relative(directory, 'index.html').read_bytes() != render_index(inventory).encode('utf-8'):
        raise ValueError('License view differs from declared component notices')
    if any(c.get('source_commit', inventory['source_commit']) != inventory['source_commit'] for c in components.values()):
        raise ValueError('Delivered component source commit differs from runtime source commit')
    selected = closure(components, inventory['packages'][package_id])
    if check_all:
        selected = set(components)
    issues = [{'component': name, 'reason': reason}
              for name in sorted(selected) for reason in components[name]['issues']]
    if formal and issues:
        raise ValueError('Unresolved distribution conditions: ' + json.dumps(issues, ensure_ascii=False))
    binaries = []
    for path in sorted(root.rglob('*')):
        name = path.relative_to(root).as_posix()
        if name.startswith(DIRECTORY + '/') or not binary(path):
            continue
        owners = {r['component'] for r in inventory['runtime_rules']
                  if r['component'] in selected and fnmatch.fnmatchcase(name, r['pattern'])}
        if len(owners) != 1:
            raise ValueError(f'Runtime needs exactly one declared owner: {name}: {sorted(owners)}')
        owner = owners.pop()
        sha = digest(path)
        if 'runtime_fingerprints' in inventory and {'component': owner, 'name': path.name, 'sha256': sha} not in inventory['runtime_fingerprints']:
            raise ValueError(f'Runtime changed after material attachment: {name}')
        binaries.append({'path': name, 'component': owner, 'sha256': sha})
    if not binaries:
        raise ValueError('Compliance validation found no runtime binaries')
    return {'schema': 1, 'package_id': package_id, 'source_commit': inventory['source_commit'],
            'distribution': 'review-only' if issues else 'evidence-complete',
            'issues': issues, 'binaries': binaries,
            'materials': sum(len(c['materials']) for c in components.values())}


def validate_archive(archive, package_id, formal=True, trusted_profiles=None):
    """Signing rechecks archive bytes; a forged green JSON report is insufficient."""
    import tempfile
    with tempfile.TemporaryDirectory(prefix='foreshadow-license-') as temporary:
        root = Path(temporary)
        with tarfile.open(archive, 'r:gz') as stream:
            stream.extractall(root, filter='data')
        if trusted_profiles is not None:
            directory = root / DIRECTORY
            inventory = load(directory / 'inventory.json')
            profile_path = relative(directory, 'reviewed-profile.json')
            sha = digest(profile_path)
            if sha != inventory.get('profile_sha256') or not any(digest(p) == sha for p in trusted_profiles.glob('*.json')):
                raise ValueError('Archive does not use a repository-reviewed platform profile')
            profile = load(profile_path)
            if inventory['packages'].get(package_id) != profile['packages'].get(package_id):
                raise ValueError('Archive package roots differ from reviewed profile')
            selected = closure(profile['components'], profile['packages'][package_id])
            if set(inventory['components']) != selected:
                raise ValueError('Archive component closure differs from reviewed profile')
            for name in selected:
                for field, expected in profile['components'][name].items():
                    if field in ('artifacts', 'repository_files'):
                        continue
                    if inventory['components'][name].get(field) != expected:
                        raise ValueError(f'Archive changed reviewed {field}: {name}')
                materials = inventory['components'][name]['materials']
                for required in profile['components'][name].get('artifacts', []):
                    if not required.get('first_party') and not any(m['sha256'] == required['sha256'] and m['role'] == required.get('role', 'source') for m in materials):
                        raise ValueError(f'Archive lacks reviewed source/notice artifact: {name}')
                for required in profile['components'][name].get('repository_files', []):
                    if not any(m['sha256'] == required['sha256'] and m['role'] == required['role'] for m in materials):
                        raise ValueError(f'Archive lacks reviewed repository material: {name}/{required["file"]}')
            rules = [r for r in profile['runtime_rules'] if r['component'] in selected]
            if inventory['runtime_rules'] != rules:
                raise ValueError('Archive changed reviewed runtime ownership')
        return validate(root, package_id, formal=formal)


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--materials', type=Path, required=True)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    facts_path = args.stage / 'share/signalscope/compliance/build-facts.json'
    prepare(args.stage, args.materials, facts_path.parent / 'providers', load(facts_path), args.source_commit)
