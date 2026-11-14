"""Build unsigned, content-addressed release entries from a tested CMake installation.

No credentials or signing key are needed in the platform build jobs. Package
composition is the only place that knows the bundled receiver/provider layout.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
from compliance import attach


def json_bytes(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, indent=2)+'\n').encode('utf-8')


def emit(root, manifest, output, base_url, stage, commit, review_only):
    attach(stage, root, manifest['id'], commit, formal=not review_only)
    (root/'toolbox.json').write_bytes(json_bytes(manifest))
    extension = 'amcpkg' if manifest['kind'] == 'application' else 'amctbx'
    filename = f"{manifest['id']}-{manifest['version']}-{manifest['platform']}.{extension}"
    archive = output/filename
    expanded = 0
    with tarfile.open(archive, 'w:gz', format=tarfile.PAX_FORMAT, dereference=False) as package:
        for path in sorted(root.rglob('*')):
            if path.is_dir() and not path.is_symlink():
                continue
            name=path.relative_to(root).as_posix()
            info=package.gettarinfo(str(path),arcname=name)
            info.uid=info.gid=0; info.uname=info.gname=''; info.mtime=0
            if info.isreg():
                expanded+=info.size
                with path.open('rb') as stream: package.addfile(info,stream)
            elif info.issym():
                path.resolve(strict=True).relative_to(root.resolve())
                package.addfile(info)
            else: raise ValueError(f'Unsupported packaged file: {name}')
    return {'manifest':manifest,'url':base_url.rstrip('/')+'/'+filename,
            'sha256':hashlib.file_digest(archive.open('rb'),'sha256').hexdigest(),
            'download_size':archive.stat().st_size,'installed_size':expanded}


def compose_application(stage, root, inventory, data_dir, mac):
    """Provider-declared optional receiver closures stay in their own downloads.

    Shared resources/runtimes used by bundled receivers remain in the base. This
    is package assembly, not an installer or UI rule about a receiver's identity.
    """
    retained, removed = set(), set()
    for entry in inventory:
        if entry['kind'] != 'receiver':
            continue
        paths = set()
        for component in entry['components']:
            matches = list(stage.rglob(Path(component['path']).name))
            if len(matches) != 1:
                raise ValueError('Receiver assembly requires one native component')
            native = matches[0]
            paths.add(native)
            runtime = native.with_name(native.stem + '.runtime.json')
            if runtime.is_file():
                paths.add(runtime)
                for name in json.loads(runtime.read_text(encoding='utf-8'))['files']:
                    if not isinstance(name,str) or Path(name).name != name or name in ('','.','..') or '\\' in name:
                        raise ValueError('Invalid receiver runtime dependency')
                    paths.add(native.parent / name)
            resource_base = data_dir / 'receivers' if mac else native.parent
            declaration = resource_base / (native.stem + '.resources.json')
            if declaration.is_file():
                paths.add(declaration)
                for name in json.loads(declaration.read_text(encoding='utf-8'))['catalogs']:
                    if not isinstance(name,str) or Path(name).name != name or name in ('','.','..') or '\\' in name:
                        raise ValueError('Invalid receiver resource path')
                    paths.add(resource_base / name)
        (retained if entry.get('bundle_with_application', True) else removed).update(paths)
    for path in sorted(removed - retained):
        relative = path.resolve().relative_to(stage.resolve())
        destination = root / relative
        if destination.is_dir():
            shutil.rmtree(destination)
        elif destination.exists():
            destination.unlink()
    selected = [entry for entry in inventory if entry.get('bundle_with_application', True)]
    (root / data_dir.relative_to(stage) / 'distribution/bundled.json').write_bytes(json_bytes(selected))


def build(stage, output, base_url, commit, release_tag=None, review_only=False, base_output=None):
    stage=stage.resolve(); output.mkdir(parents=True,exist_ok=True)
    if base_output is not None and base_output.exists():
        raise ValueError('Use a new base application output directory')
    mac=(stage/'Foreshadow.app').is_dir()
    bin_dir=stage/('Foreshadow.app/Contents/MacOS' if mac else 'bin')
    suffix='.exe' if (bin_dir/'signalscope.exe').is_file() else ''
    info=json.loads(subprocess.check_output([str(bin_dir/('signalscope'+suffix)), '--build-info'],text=True))
    if release_tag is not None and (release_tag!='v'+info['version'] or base_url.rstrip('/').rsplit('/',1)[-1]!=release_tag):
        raise ValueError('Release tag, application version and asset URL must match')
    data_dir=stage/('Foreshadow.app/Contents/Resources/signalscope' if mac else 'share/signalscope')
    inventory=json.loads((data_dir/'distribution/bundled.json').read_text(encoding='utf-8'))
    entries=[]
    with tempfile.TemporaryDirectory(prefix='amc-release-') as temporary:
        work=Path(temporary)
        app_root=work/'application'
        if mac:
            # Match the standalone app archive: the bundle owns every runtime,
            # resource and dependency notice. Root-level staging duplicates
            # are development/install outputs, not a second portable app.
            app_root.mkdir()
            shutil.copytree(stage/'Foreshadow.app',app_root/'Foreshadow.app',symlinks=True)
        else:
            shutil.copytree(stage,app_root,symlinks=True,ignore=shutil.ignore_patterns('research-plugins'))
        compose_application(stage,app_root,inventory,data_dir,mac)
        application=next(x.copy() for x in inventory if x['kind']=='application')
        relative=bin_dir.relative_to(stage).as_posix()+'/'
        application['entrypoints']={role:relative+name+suffix for role,name in {
            'gui':'Foreshadow' if mac else 'signalscope-gui','cli':'signalscope','updater':'signalscope-update',
            'launcher':'signalscope-launcher'}.items()}
        if suffix: application['entrypoints']['gui_launcher']=relative+'signalscope-launcher-gui.exe'
        application['actions']=['update','rollback']
        application['build']={'source_commit':commit}
        for entry in application['entrypoints'].values():
            if not (app_root/entry).is_file(): raise ValueError(f'Missing application entrypoint: {entry}')
        entries.append(emit(app_root,application,output,base_url,stage,commit,review_only))
        if base_output is not None:
            shutil.copytree(app_root,base_output,symlinks=True)
        for bundled in inventory:
            if bundled['kind']!='receiver': continue
            manifest=bundled.copy(); manifest['actions']=['update','enable','disable','uninstall','rollback']
            manifest['build']={'source_commit':commit}
            package_root=work/bundled['id']; package_root.mkdir()
            for component in manifest['components']:
                filename=Path(component['path']).name
                matches=list(stage.rglob(filename))
                if len(matches)!=1: raise ValueError(f'Expected one native receiver module: {filename}')
                native=matches[0]; destination=package_root/('Foreshadow.app/Contents/MacOS/receiver-plugins' if mac else 'receiver')
                destination.mkdir(parents=True,exist_ok=True)
                shutil.copy2(native,destination/native.name)
                runtime_list=native.parent/(native.stem+'.runtime.json')
                if runtime_list.is_file():
                    runtime=json.loads(runtime_list.read_text(encoding='utf-8'))
                    if not isinstance(runtime,dict) or runtime.get('schema')!=1 or not isinstance(runtime.get('files'),list):
                        raise ValueError('Unsupported receiver runtime declaration')
                    for dependency in runtime['files']:
                        if not isinstance(dependency,str) or dependency in ('','.','..') or Path(dependency).name!=dependency or '\\' in dependency:
                            raise ValueError('Invalid receiver runtime dependency')
                        shutil.copy2(native.parent/dependency,destination/dependency)
                resource_base=(data_dir/'receivers') if mac else native.parent
                declaration=resource_base/(native.stem+'.resources.json')
                resource=json.loads(declaration.read_text(encoding='utf-8')) if declaration.is_file() else {
                    'schema':1,'catalogs':[native.stem+'.i18n','amc-receiver-common.i18n']}
                if resource.get('schema')!=1 or not isinstance(resource.get('catalogs'),list):
                    raise ValueError('Unsupported receiver resource declaration')
                resource_destination=(package_root/'Foreshadow.app/Contents/Resources/signalscope/receivers') if mac else destination
                for catalog_name in resource['catalogs']:
                    if not isinstance(catalog_name,str) or catalog_name in ('','.','..') or Path(catalog_name).name!=catalog_name or '\\' in catalog_name:
                        raise ValueError('Invalid receiver catalog resource')
                    shutil.copytree(resource_base/catalog_name,resource_destination/catalog_name,dirs_exist_ok=True)
                if 'application' in resource:manifest['application']=resource['application']
                component['path']=(destination/native.name).relative_to(package_root).as_posix()
            if mac:
                # Preserve the deployed @loader_path/Frameworks layout for native dependencies.
                shutil.copytree(stage/'Foreshadow.app/Contents/Frameworks',package_root/'Foreshadow.app/Contents/Frameworks',symlinks=True)
                # Notices and source/relink material travel with the copied
                # runtime closure, including runtimes contributed by providers.
                notices=stage/'Foreshadow.app/Contents/Resources/licenses'
                if notices.is_dir():
                    shutil.copytree(notices,package_root/'Foreshadow.app/Contents/Resources/licenses',symlinks=True)
            entries.append(emit(package_root,manifest,output,base_url,stage,commit,review_only))
        research_root=stage/'research-plugins/power-report'
        if research_root.is_dir():
            package_root=work/'power-report'; shutil.copytree(research_root,package_root/'research')
            if suffix:
                for runtime in ('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll'):
                    shutil.copy2(bin_dir/runtime,package_root/'research'/runtime)
            plugin=json.loads((research_root/'power-report.json').read_text(encoding='utf-8'))
            manifest={'schema':1,'id':'org.amc.research.power-report','kind':'research','version':info['version'],
                'platform':info['platform'],'name':plugin['title'],'description':plugin['title'],
                'application':{'minimum':'0.2.0','maximum':'0.3.0'},'dependencies':[],
                'build':{'source_commit':commit},'actions':['enable','disable','uninstall','rollback','update'],
                'components':[{'kind':'research','id':plugin['id'],'path':'research/power-report.json'}]}
            entries.append(emit(package_root,manifest,output,base_url,stage,commit,review_only))
    (output/f"entries-{info['platform']}.json").write_bytes(json_bytes(entries))
    print(json.dumps({'version':info['version'],'platform':info['platform'],'packages':len(entries)}))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',type=Path,required=True); parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--base-url',required=True); parser.add_argument('--source-commit',required=True)
    parser.add_argument('--release-tag')
    parser.add_argument('--base-output',type=Path,help='Write the qualified composed base for portable archives; destination must be new')
    parser.add_argument('--review-only',action='store_true',help='Preserve unresolved authorization conditions; never eligible for official signing')
    args=parser.parse_args(); build(args.stage,args.output,args.base_url,args.source_commit,args.release_tag,args.review_only,args.base_output)
