"""Sign a reviewed release catalog locally; private keys never enter build jobs."""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from compliance import validate_archive


def sign(entries_files, key, output, previous=None, sequence=None, openssl='openssl'):
    repository=Path(__file__).resolve().parents[2]
    key=key.resolve(strict=True)
    if key.is_relative_to(repository): raise ValueError('Keep the private signing key outside the source checkout')
    public=subprocess.check_output([openssl,'pkey','-in',str(key),'-pubout','-outform','DER'])
    raw=public[-32:]
    anchor=json.loads((repository/'resources/distribution/trust.json').read_text(encoding='utf-8'))
    if raw.hex()!=anchor['public_key_hex']: raise ValueError('Signing key does not match the compiled trust anchor')
    entries={}; old_sequence=0; revalidated=set()
    with tempfile.TemporaryDirectory(prefix='amc-sign-') as tmp:
        tmp=Path(tmp); (tmp/'public.der').write_bytes(public)
        if previous:
            envelope=json.loads(previous.read_text(encoding='utf-8'))
            payload=envelope['payload'].encode('utf-8')
            (tmp/'previous').write_bytes(payload);(tmp/'signature').write_bytes(bytes.fromhex(envelope['signature']))
            subprocess.run([openssl,'pkeyutl','-verify','-rawin','-pubin','-keyform','DER','-inkey',str(tmp/'public.der'),
                '-in',str(tmp/'previous'),'-sigfile',str(tmp/'signature')],check=True,stdout=subprocess.DEVNULL)
            data=json.loads(payload);old_sequence=data['sequence']
            for entry in data['packages']:
                m=entry['manifest'];entries[(m['id'],m['version'],m['platform'])]=entry
        for filename in entries_files:
            for entry in json.loads(filename.read_text(encoding='utf-8')):
                manifest=entry['manifest']
                asset=filename.parent/entry['url'].rsplit('/',1)[-1]
                with asset.open('rb') as stream: digest=hashlib.file_digest(stream,'sha256').hexdigest()
                if digest!=entry['sha256'] or asset.stat().st_size!=entry['download_size']: raise ValueError(f'Asset mismatch: {asset.name}')
                validate_archive(asset, manifest['id'], formal=True,
                                 trusted_profiles=repository/'resources/licensing/profiles')
                if not entry['url'].startswith('https://github.com/vicliu624/Foreshadow/releases/download/'): raise ValueError('Official assets must use this repository Releases')
                identity=(manifest['id'],manifest['version'],manifest['platform'])
                entries[identity]=entry;revalidated.add(identity)
        if set(entries)-revalidated:
            raise ValueError('Retained catalog entries require locally supplied archives and reviewed profiles for license revalidation')
        number=int(time.time()) if sequence is None else sequence
        if number<=old_sequence: raise ValueError('Catalog sequence must increase')
        data={'schema':1,'sequence':number,'published':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'packages':[entries[key] for key in sorted(entries)]}
        payload=json.dumps(data,ensure_ascii=False,separators=(',',':')).encode('utf-8')
        (tmp/'payload').write_bytes(payload)
        subprocess.run([openssl,'pkeyutl','-sign','-rawin','-inkey',str(key),'-in',str(tmp/'payload'),'-out',str(tmp/'signature')],check=True)
        signature=(tmp/'signature').read_bytes()
        subprocess.run([openssl,'pkeyutl','-verify','-rawin','-pubin','-keyform','DER','-inkey',str(tmp/'public.der'),
            '-in',str(tmp/'payload'),'-sigfile',str(tmp/'signature')],check=True,stdout=subprocess.DEVNULL)
        output.write_text(json.dumps({'schema':1,'key_id':hashlib.sha256(raw).hexdigest()[:16],
            'payload':payload.decode('utf-8'),'signature':signature.hex()},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'sequence':number,'packages':len(entries),'catalog':str(output)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--entries',type=Path,nargs='+',required=True)
    parser.add_argument('--key',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--previous',type=Path);parser.add_argument('--sequence',type=int)
    parser.add_argument('--openssl',default='openssl')
    args=parser.parse_args();sign(args.entries,args.key,args.output,args.previous,args.sequence,args.openssl)
