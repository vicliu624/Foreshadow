"""Compare the installed Debian SDK with the source descriptors in reviewed materials."""
import argparse
import json
from pathlib import Path
import re
import subprocess

from compliance import digest, load, relative


def installed_sources():
    output = subprocess.check_output(
        ['dpkg-query', '--show', '--showformat=${source:Package}\t${source:Version}\t${db:Status-Abbrev}\n'],
        text=True, encoding='utf-8')
    result = {}
    for line in output.splitlines():
        fields = line.split('\t')
        if len(fields) == 3 and fields[2][1:2] == 'i' and fields[0] and fields[1]:
            result.setdefault(fields[0], set()).add(fields[1])
    return result


def verify(materials, installed):
    evidence = load(materials / 'materials.json')
    checked = {}
    for component in evidence['components'].values():
        for item in component['materials']:
            if not item['path'].endswith('.dsc'):
                continue
            path = relative(materials, item['path'])
            if digest(path) != item['sha256']:
                raise ValueError(f'Source descriptor checksum mismatch: {item["path"]}')
            text = path.read_text(encoding='utf-8')
            names = re.findall(r'^Source: (\S+)\s*$', text, re.MULTILINE)
            versions = re.findall(r'^Version: (\S+)\s*$', text, re.MULTILINE)
            if len(names) != 1 or len(versions) != 1:
                raise ValueError(f'Invalid Debian source descriptor: {item["path"]}')
            name, expected = names[0], versions[0]
            actual = installed.get(name, set())
            if actual != {expected}:
                raise ValueError(f'Debian SDK source mismatch for {name}: reviewed {expected}, installed {sorted(actual)}')
            if name in checked and checked[name] != expected:
                raise ValueError(f'Conflicting reviewed source descriptors: {name}')
            checked[name] = expected
    if not checked:
        raise ValueError('No reviewed Debian source descriptors were checked')
    return checked


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--materials', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = {'schema': 1, 'checked_sources': verify(args.materials, installed_sources()),
              'scope': 'Installed SDK source identities and descriptor hashes; not upstream OpenPGP verification'}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
