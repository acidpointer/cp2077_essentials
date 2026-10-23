"""Release naming, asset completeness and failure preservation."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import package
import release
import verification


class ArchiveNames(unittest.TestCase):
    def test_names_describe_version_and_installation_layout(self):
        self.assertEqual(package.archive_name('red4ext', '1.30.0', 'Release', 'runtime'),
                         'RED4ext-1.30.0-game2.5-mo2-rootbuilder.zip')
        self.assertEqual(package.archive_name('red4ext', '1.30.0', 'Release', 'standard'),
                         'RED4ext-1.30.0-game2.5-game-root.zip')
        self.assertEqual(package.archive_name('codeware', '1.20.6', 'Release', 'runtime'),
                         'Codeware-1.20.6-game2.5.zip')
        self.assertEqual(package.archive_name('archive-xl', '1.27.5', 'Release', 'symbols'),
                         'ArchiveXL-1.27.5-game2.5-symbols.zip')
        self.assertEqual(package.archive_name('tweak-xl', '1.11.5', 'Debug', 'runtime'),
                         'TweakXL-1.11.5-game2.5-debug.zip')

    def test_version_suffixes_cannot_leak_into_names(self):
        with self.assertRaises(ValueError):
            package.archive_name('codeware', '1.20.6-dirty', 'Release', 'runtime')
        with tempfile.TemporaryDirectory() as directory:
            build = pathlib.Path(directory)
            resource = build / 'src/loader/Resource.rc'
            resource.parent.mkdir(parents=True)
            for suffix in ['+HEAD.1.47ac01c', '-dirty', '+abcdef-dirty']:
                resource.write_text(f'VALUE "ProductVersion", "1.30.0{suffix}"')
                self.assertEqual(package.original_version('red4ext', build, build), '1.30.0')

    def test_packaging_keeps_loader_layouts_separate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            build = root / 'build/red4ext/release'
            resource = build / 'src/loader/Resource.rc'
            resource.parent.mkdir(parents=True)
            resource.write_text('VALUE "ProductVersion", "1.30.0+HEAD.dirty"')
            payload = ['bin/x64/winmm.dll', 'red4ext/RED4ext.dll', 'red4ext/LICENSE.txt',
                       'red4ext/THIRD_PARTY_LICENSES.txt', 'bin/x64/winmm.pdb', 'red4ext/RED4ext.pdb']
            for name in payload:
                path = build / 'install' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(name.encode())
            with patch.object(c, 'ROOT', root), patch.object(c, 'config', return_value={'projects': {'red4ext': {'path': 'sources/red4ext'}}}), patch.object(package.patches, 'identity', return_value={'fingerprint': 'fixture'}):
                record = package.create('red4ext', 'Release')
            self.assertEqual(record['version'], '1.30.0')
            runtime = {name for name in payload if not name.endswith('.pdb')}
            for kind, expected in [('runtime', {'Root/' + name for name in runtime}),
                                   ('standard', runtime),
                                   ('symbols', {name for name in payload if name.endswith('.pdb')})]:
                with zipfile.ZipFile(root / record['files'][kind]['file']) as archive:
                    self.assertEqual(set(archive.namelist()), expected)


    def test_verifier_rejects_old_names_and_wrong_versions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / 'sources/codeware'
            source.mkdir(parents=True)
            (source / 'xmake.lua').write_text('set_version("1.20.6")')
            record = {'source': {}, 'sdk': {}, 'config': 'Release', 'version': '1.20.6',
                      'files': {'runtime': {'file': 'Codeware-1.20.6-dirty.zip'}}}
            manifest = {'schema': 2, 'config': 'Release', 'sdk': {}, 'projects': {'codeware': record}}
            with patch.object(c, 'ROOT', root), patch.object(c, 'config', return_value={'projects': {'codeware': {'path': 'sources/codeware'}}}), patch.object(verification.patches, 'identity', return_value={}):
                (root / 'manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError, 'unexpected archive name'):
                    verification.distribution(root)
                record['version'] = '1.20.5'
                (root / 'manifest.json').write_text(json.dumps(manifest))
                with self.assertRaisesRegex(ValueError, 'version/configuration'):
                    verification.distribution(root)


class ReleaseAssets(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cp2077-release-tests-')
        self.root = pathlib.Path(self.temporary.name)
        self.root_patch = patch.object(c, 'ROOT', self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)
        self.addCleanup(self.temporary.cleanup)
        self.source = self.root / 'dist/release'
        self.source.mkdir(parents=True)
        self.manifest = {'schema': 2, 'config': 'Release', 'projects': {}}
        versions = {'red4ext': '1.30.0', 'codeware': '1.20.6', 'tweak-xl': '1.11.5', 'archive-xl': '1.27.5'}
        for project, version in versions.items():
            record = {'project': c.NAMES[project], 'version': version, 'files': {}}
            for kind in ['runtime', 'symbols'] + (['standard'] if project == 'red4ext' else []):
                group = '' if kind == 'runtime' else kind
                path = self.source / group / package.archive_name(project, version, 'Release', kind)
                path.parent.mkdir(parents=True, exist_ok=True)
                with zipfile.ZipFile(path, 'w') as archive:
                    archive.writestr('fixture.txt', project + kind)
                record['files'][kind] = {'file': path.relative_to(self.source).as_posix(), 'sha256': c.sha(path)}
            self.manifest['projects'][project] = record
        self.save()
        self.verification = patch.object(release.verification, 'distribution')
        self.verify = self.verification.start()
        self.addCleanup(self.verification.stop)

    def save(self):
        (self.source / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_all_archives_flattened_and_checksummed(self):
        destination = release.prepare('v1.0.0')
        self.verify.assert_called_once_with(self.source)
        assets = (destination / 'assets.txt').read_text().splitlines()
        self.assertEqual(len(assets), 20)  # Nine ZIPs, nine sidecars, manifest and SHA256SUMS.
        self.assertTrue(all((self.root / name).is_file() for name in assets))
        manifest = json.loads((destination / 'manifest.json').read_text())
        self.assertEqual(manifest['essentials_release'], 'v1.0.0')
        self.assertEqual(len(list(destination.glob('*.zip'))), 9)
        for record in manifest['projects'].values():
            for item in record['files'].values():
                self.assertNotIn('/', item['file'])
                self.assertEqual(c.sha(destination / item['file']), item['sha256'])
        result = subprocess.run(['sha256sum', '--check', 'SHA256SUMS'], cwd=destination, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_missing_project_or_symbols_refuses_release(self):
        self.manifest['projects'].pop('codeware')
        self.save()
        with self.assertRaisesRegex(ValueError, 'all four'):
            release.prepare('v1.0.0')
        self.manifest['projects']['codeware'] = {'files': {}}
        self.save()
        with self.assertRaisesRegex(ValueError, 'incomplete'):
            release.prepare('v1.0.0')

    def test_corrupt_archive_preserves_prepared_release(self):
        destination = release.prepare('v1.0.0')
        before = {p.name: p.read_bytes() for p in destination.iterdir()}
        runtime = self.manifest['projects']['codeware']['files']['runtime']['file']
        (self.source / runtime).write_bytes(b'corrupted')
        with self.assertRaisesRegex(ValueError, 'changed'):
            release.prepare('v1.0.0')
        self.assertEqual({p.name: p.read_bytes() for p in destination.iterdir()}, before)

    def test_invalid_tag_rejected_and_rc_accepted(self):
        for tag in ['main', 'v1.0', 'v1.0.0/escape', 'v1.0.0-rc;false']:
            with self.assertRaises(ValueError):
                release.prepare(tag)
        release.prepare('v1.0.0-rc.1')


class SourceBoundaries(unittest.TestCase):
    def test_tracked_copied_source_is_rejected(self):
        spec = importlib.util.spec_from_file_location('check_sources', pathlib.Path(__file__).resolve().parents[1] / 'scripts/check-sources.py')
        audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(audit)
        result = subprocess.CompletedProcess([], 0, stdout=b'100644 ' + b'0' * 40 + b' 0\tsources/codeware/copied.cpp\0')
        with patch.object(c, 'config', return_value={'projects': dict.fromkeys(audit.UPSTREAMS)}), patch.object(c, 'git', return_value=result):
            with self.assertRaisesRegex(ValueError, 'do not vendor'):
                audit.audit()


if __name__ == '__main__':
    unittest.main()
