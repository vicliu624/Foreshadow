"""Independent-provider and negative distribution evidence contracts."""
from pathlib import Path
import copy
import json
import sys
import tempfile
import unittest
import shutil

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/distribution'))
from compliance import DIRECTORY, attach, digest, dump, prepare, validate, validate_archive
from collect_materials import collect
from package_release import compose_application
from portable_archive import export


class ComplianceContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='许可 📡 ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'stage'
        self.materials = self.root / 'materials'
        self.declarations = self.root / 'declarations'
        self.declarations.mkdir()
        self.stage.mkdir()
        self.materials.mkdir()
        self.component = {'id': 'independent-provider', 'version': '7', 'license': 'MIT',
                          'origin': 'https://example.invalid/source', 'source_required': True,
                          'display_notice': 'Independent provider copyright notice',
                          'issues': [], 'dependencies': [], 'materials': []}
        for role in ('source', 'build', 'notice'):
            path = self.materials / (role + '.txt')
            path.write_text(role, encoding='utf-8')
            self.component['materials'].append({'role': role, 'path': path.name, 'sha256': digest(path)})
        self.evidence = {'schema': 1, 'platform': 'contract', 'application_id': 'new.app',
                         'build_facts': {'qt': 'test'},
                         'components': {'independent-provider': self.component},
                         'runtime_rules': [{'component': 'independent-provider', 'pattern': 'plugin/native.so'}]}
        dump(self.materials / 'materials.json', self.evidence)
        dump(self.declarations / 'new-provider.json', {'schema': 1,
             'packages': {'new.app': ['independent-provider'], 'new.plugin': ['independent-provider']},
             'application': ['independent-provider']})
        (self.stage / 'plugin').mkdir()
        (self.stage / 'plugin/native.so').write_bytes(b'\x7fELF contract')

    def prepare(self):
        dump(self.materials / 'materials.json', self.evidence)
        prepare(self.stage, self.materials, self.declarations, {'platform': 'contract', 'qt': 'test'}, 'commit')

    def test_new_provider_and_independent_package_without_collector_changes(self):
        self.prepare()
        package = self.root / 'package'
        (package / 'plugin').mkdir(parents=True)
        (package / 'plugin/native.so').write_bytes(b'\x7fELF contract')
        result = attach(self.stage, package, 'new.plugin', 'commit', formal=True)
        self.assertEqual(result['distribution'], 'evidence-complete')
        self.assertEqual(result['binaries'][0]['component'], 'independent-provider')
        self.assertTrue((package / DIRECTORY / 'source.txt').exists())
        page = (package / DIRECTORY / 'index.html').read_text(encoding='utf-8')
        self.assertIn('Independent provider copyright notice', page)
        self.assertNotIn('uses Qt', page)
        self.assertNotIn('uses FFmpeg', page)

    def test_optional_provider_does_not_block_or_leak_into_independent_base(self):
        # An independently added provider has an unresolved distribution issue.
        # Its download remains blocked; the base retains only its own closure.
        base_component=copy.deepcopy(self.component);base_component['id']='base';base_component['issues']=[]
        self.component['issues']=['External permission required']
        self.evidence['components']['base']=base_component
        self.evidence['runtime_rules'] += [{'component':'base','pattern':'receiver/base.so'}]
        dump(self.declarations/'new-provider.json',{'schema':1,'packages':{'new.app':['base'],'new.plugin':['independent-provider']},'application':['base']})
        native=self.stage/'receiver';native.mkdir()
        (native/'base.so').write_bytes(b'\x7fELF base')
        (native/'optional.so').write_bytes((self.stage/'plugin/native.so').read_bytes())
        (self.stage/'plugin/native.so').unlink();self.evidence['runtime_rules'][0]['pattern']='receiver/optional.so'
        for name in ['base','optional']:
            dump(native/(name+'.resources.json'),{'schema':1,'catalogs':['common.i18n',name+'.i18n']})
            (native/(name+'.i18n')).mkdir()
        (native/'common.i18n').mkdir();(native/'common.i18n/en.json').write_text('{}')
        data=self.stage/'share/signalscope';(data/'distribution').mkdir(parents=True)
        entries=[{'kind':'application','id':'new.app'},
                 {'kind':'receiver','id':'base.receiver','components':[{'path':'receiver/base.so'}]},
                 {'kind':'receiver','id':'optional.receiver','bundle_with_application':False,'components':[{'path':'receiver/optional.so'}]}]
        dump(data/'distribution/bundled.json',entries)
        self.prepare()
        app=self.root/'base-app';shutil.copytree(self.stage,app)
        compose_application(self.stage,app,entries,data,False)
        result=attach(self.stage,app,'new.app','commit',formal=True)
        self.assertEqual(result['distribution'],'evidence-complete')
        self.assertTrue((app/'receiver/base.so').is_file())
        self.assertTrue((app/'receiver/common.i18n/en.json').is_file())
        self.assertFalse((app/'receiver/optional.so').exists())
        self.assertFalse((app/'receiver/optional.i18n').exists())
        self.assertNotIn('independent-provider',json.loads((app/DIRECTORY/'inventory.json').read_text())['components'])
        optional=self.root/'optional';(optional/'receiver').mkdir(parents=True)
        shutil.copy2(native/'optional.so',optional/'receiver/optional.so')
        with self.assertRaisesRegex(ValueError,'Unresolved distribution'):
            attach(self.stage,optional,'new.plugin','commit',formal=True)

    def test_missing_source_rejected_even_for_review(self):
        self.component['materials'] = [m for m in self.component['materials'] if m['role'] != 'source']
        with self.assertRaisesRegex(ValueError, 'Missing'):
            self.prepare()

    def test_portable_base_preserves_epoch_members_and_unicode_bytes(self):
        import os
        import zipfile
        import tarfile
        self.prepare();app=self.root/'portable-base';shutil.copytree(self.stage,app)
        attach(self.stage,app,'new.app','commit',formal=True)
        dump(app/'toolbox.json',{'id':'new.app','kind':'application'})
        extra=app/'资源 📡.txt';extra.write_bytes('Original portable bytes'.encode('utf-8'));os.utime(extra,(0,0))
        target=self.root/'base.zip';export(app,target)
        with zipfile.ZipFile(target) as archive:
            self.assertEqual(archive.read(extra.name),extra.read_bytes())
            self.assertEqual(archive.getinfo(extra.name).date_time,(1980,1,1,0,0,0))
        target=self.root/'base.tar.gz';export(app,target)
        with tarfile.open(target) as archive:
            self.assertEqual(archive.extractfile(extra.name).read(),extra.read_bytes())
            self.assertEqual(archive.getmember(extra.name).mtime,0)

    def test_raw_all_provider_stage_is_not_a_portable_base(self):
        self.prepare()
        with self.assertRaisesRegex(ValueError,'composed application'):
            export(self.stage,self.root/'must-not-publish.zip')

    def test_deep_upstream_notices_keep_portable_paths_and_original_provenance(self):
        import io
        import tarfile
        repository = self.root / 'repository'
        repository.mkdir()
        for name in ('LICENSE', 'LICENSING.md', 'LICENSING.zh-CN.md', 'THIRD_PARTY_NOTICES.md',
                     'THIRD_PARTY_NOTICES.zh-CN.md', 'SOURCE_AND_REPLACEMENT.md', 'SOURCE_AND_REPLACEMENT.zh-CN.md'):
            (repository / name).write_text('Contract material', encoding='utf-8')
        archive = self.materials / 'upstream.tar.gz'
        member = 'upstream/' + 'very-long-original-directory/' * 15 + 'COPYING'
        with tarfile.open(archive, 'w:gz') as stream:
            info = tarfile.TarInfo(member)
            info.size = len(b'Original copyright and terms')
            stream.addfile(info, io.BytesIO(b'Original copyright and terms'))
            spdx = tarfile.TarInfo('upstream/LICENSES/BSD-3-Clause.txt')
            spdx.size = len(b'Original SPDX license text')
            stream.addfile(spdx, io.BytesIO(b'Original SPDX license text'))
        component = {'version': '1', 'license': 'MIT', 'origin': 'contract', 'source_required': True,
                     'dependencies': [], 'issues': [],
                     'artifacts': [{'file': archive.name, 'sha256': digest(archive), 'extract_notices': True}],
                     'repository_files': [{'file': 'SOURCE_AND_REPLACEMENT.md', 'role': 'build',
                                            'sha256': digest(repository / 'SOURCE_AND_REPLACEMENT.md')}]}
        dump(self.root / 'profile.json', {'schema': 1, 'components': {'new-provider': component}})
        output = self.root / 'delivered'
        collect(self.root / 'profile.json', self.materials, repository, archive, 'commit', output)
        delivered = json.loads((output / 'materials.json').read_text())
        notices = [m for m in delivered['components']['new-provider']['materials'] if m['role'] == 'notice']
        self.assertEqual(len(notices), 2)
        self.assertTrue(all(len(n['path']) < 80 for n in notices))
        original = next(n for n in notices if n['origin_member'] == member)
        self.assertEqual((output / original['path']).read_bytes(), b'Original copyright and terms')
        self.assertTrue(any(n['origin_member'].endswith('LICENSES/BSD-3-Clause.txt') for n in notices))

    def test_changed_material_rejected(self):
        (self.materials / 'source.txt').write_text('changed')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            self.prepare()

    def test_unknown_binary_rejected(self):
        (self.stage / 'plugin/new.so').write_bytes(b'\x7fELF surprise')
        with self.assertRaisesRegex(ValueError, 'declared owner'):
            self.prepare()

    def test_changed_runtime_rejected_after_material_attachment(self):
        self.prepare()
        (self.stage / 'plugin/native.so').write_bytes(b'\x7fELF different')
        with self.assertRaisesRegex(ValueError, 'changed after material'):
            validate(self.stage, 'new.app', formal=False)

    def test_prominent_notice_cannot_be_removed_from_license_view(self):
        self.prepare()
        (self.stage / DIRECTORY / 'index.html').write_text('No notices', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'License view differs'):
            validate(self.stage, 'new.app', formal=False)

    def test_erasing_an_issue_cannot_change_repository_review(self):
        import tarfile
        self.component['issues'] = ['Permission required']
        self.evidence['packages'] = {'new.app': ['independent-provider'], 'new.plugin': ['independent-provider']}
        reviewed = copy.deepcopy(self.evidence)
        trusted = self.root / 'trusted'
        dump(trusted / 'profile.json', reviewed)
        dump(self.materials / 'reviewed-profile.json', reviewed)
        self.evidence['profile_sha256'] = digest(trusted / 'profile.json')
        self.prepare()
        package = self.root / 'package'
        (package / 'plugin').mkdir(parents=True)
        (package / 'plugin/native.so').write_bytes(b'\x7fELF contract')
        attach(self.stage, package, 'new.plugin', 'commit', formal=False)
        path = package / DIRECTORY / 'inventory.json'
        value = json.loads(path.read_text())
        value['components']['independent-provider']['issues'] = []
        dump(path, value)
        archive = self.root / 'tampered.amctbx'
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(package, arcname='')
        with self.assertRaisesRegex(ValueError, 'changed reviewed issues'):
            validate_archive(archive, 'new.plugin', trusted_profiles=trusted)

    def test_unresolved_conditions_are_visible_and_block_formal_distribution(self):
        self.component['issues'] = ['Vendor permission remains unverified']
        self.prepare()
        review = validate(self.stage, 'new.app', formal=False)
        self.assertEqual(review['distribution'], 'review-only')
        self.assertIn('Vendor', review['issues'][0]['reason'])
        with self.assertRaisesRegex(ValueError, 'Unresolved distribution'):
            validate(self.stage, 'new.app', formal=True)

    def test_build_version_mismatch_rejected(self):
        self.evidence['build_facts']['qt'] = 'other'
        with self.assertRaisesRegex(ValueError, 'version mismatch'):
            self.prepare()

    def test_traversal_material_rejected(self):
        self.component['materials'][0]['path'] = '../private.txt'
        with self.assertRaisesRegex(ValueError, 'Invalid compliance path'):
            self.prepare()

    def test_commit_mismatch_rejected(self):
        self.prepare()
        with self.assertRaisesRegex(ValueError, 'commit differs'):
            attach(self.stage, self.root / 'output', 'new.plugin', 'another', formal=False)

    def test_unknown_component_rejected(self):
        self.component['dependencies'] = ['undeclared']
        with self.assertRaisesRegex(ValueError, 'Missing component'):
            self.prepare()

    def test_signing_rechecks_material_in_archive(self):
        import tarfile
        self.prepare()
        (self.stage / DIRECTORY / 'source.txt').write_text('tampered after packaging')
        archive = self.root / 'package.amctbx'
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(self.stage, arcname='')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            validate_archive(archive, 'new.app')


if __name__ == '__main__':
    unittest.main()
