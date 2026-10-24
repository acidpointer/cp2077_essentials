"""CET installation layouts, resource pins and rejected stale/extra payloads."""
import json
import contextlib
import io
import pathlib
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import package
import verification


class CetPayload(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='cp2077-cet-tests-')
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)
        self.source = self.root / 'sources/cyber-engine-tweaks'
        self.build = self.root / 'build/plugins/release'
        self.data_prefix = 'bin/x64/plugins/cyber_engine_tweaks/'
        self.plugin_prefix = 'red4ext/plugins/CyberEngineTweaks/'
        self.expected = {}

        def input_file(path, archive_path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            if archive_path:
                self.expected[archive_path] = data

        input_file(self.build / 'bin/CyberEngineTweaks.dll', self.plugin_prefix + 'CyberEngineTweaks.dll', b'built DLL')
        input_file(self.build / 'bin/CyberEngineTweaks.pdb', None, b'built PDB')
        input_file(self.source / 'LICENSE', self.plugin_prefix + 'LICENSE', b'upstream license')
        for path in ['ThirdParty_LICENSES', 'scripts/json/json.lua', 'scripts/json/LICENSE',
                     'scripts/IconGlyphs/icons.lua', 'fonts/NotoSans-Regular.ttf']:
            input_file(self.source / path, self.data_prefix + path, path.encode())
        # Independent fixture notice set includes the otherwise easily missed
        # FreeType notices and exact headers/static libraries used by CET.
        notices = {'cet-json': ['LICENSE.MIT'], 'cet-mem': ['LICENSE.txt'],
                   'cet-sol2': ['LICENSE.txt'], 'cet-luajit': ['COPYRIGHT'],
                   'cet-imgui': ['LICENSE.txt'], 'cet-xbyak': ['COPYRIGHT'],
                   'cet-stb': ['LICENSE'], 'cet-freetype': ['docs/FTL.TXT', 'docs/GPLv2.TXT'],
                   'tiltedcore': ['LICENSE'], 'mimalloc': ['LICENSE'], 'minhook': ['LICENSE.txt'],
                   'hopscotch-map': ['LICENSE'], 'spdlog': ['LICENSE']}
        dependencies = {}
        for name, paths in notices.items():
            dependencies[name] = {'path': 'build/deps/' + name, 'commit': name + '-pin'}
            for path in paths:
                input_file(self.root / dependencies[name]['path'] / path,
                           self.plugin_prefix + 'licenses/' + name + '/' + pathlib.Path(path).name,
                           (name + '/' + path).encode())
        self.asset = self.root / 'build/cet-assets.zip'
        with zipfile.ZipFile(self.asset, 'w') as archive:
            for name in ['tweakdb/usedhashes.kark', 'tweakdb/tweakdbstr.kark']:
                archive.writestr(self.data_prefix + name, name.encode())
                self.expected[self.data_prefix + name] = name.encode()
            archive.writestr('bin/x64/version.dll', b'NEVER reuse bundled loader')
            archive.writestr('bin/x64/plugins/cyber_engine_tweaks.asi', b'NEVER reuse prebuilt CET')
        self.lock = {'projects': {'cyber-engine-tweaks': {'path': 'sources/cyber-engine-tweaks', 'version': '1.38.0'}},
                     'dependencies': dependencies, 'sqlite_asset': {'version': '3.46.1', 'sha256': 'sqlite-pin'},
                     'cet_assets': {'path': 'build/cet-assets.zip', 'sha256': c.sha(self.asset)}}
        for mocked in [patch.object(c, 'ROOT', self.root), patch.object(c, 'config', return_value=self.lock),
                       patch.object(package.patches, 'identity', return_value={}),
                       patch.object(verification, 'binary'),
                       patch.object(verification, 'exports', return_value={'Main', 'Query', 'Supports'})]:
            mocked.start()
            self.addCleanup(mocked.stop)
        self.record = package.create('cyber-engine-tweaks', 'Release')
        self.manifest = {'schema': 2, 'config': 'Release', 'sdk': {},
                         'projects': {'cyber-engine-tweaks': self.record}}
        self.save()

    def save(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_complete_rootbuilder_standard_and_symbol_layouts(self):
        for kind, prefix in [('runtime', 'Root/'), ('standard', '')]:
            with zipfile.ZipFile(self.root / self.record['files'][kind]['file']) as archive:
                self.assertEqual({name: archive.read(name) for name in archive.namelist()},
                                 {prefix + name: data for name, data in self.expected.items()})
        with zipfile.ZipFile(self.root / self.record['files']['symbols']['file']) as archive:
            self.assertEqual(archive.namelist(), [self.plugin_prefix + 'CyberEngineTweaks.pdb'])
        verification.distribution(self.root)

    def test_extra_asi_loader_or_missing_metadata_is_rejected(self):
        item = self.record['files']['runtime']
        path = self.root / item['file']
        for extra in [False, True]:
            payload = {'Root/' + name: data for name, data in self.expected.items()}
            if extra:
                payload['Root/bin/x64/version.dll'] = b'unwanted duplicate loader'
            else:
                del payload['Root/' + self.data_prefix + 'tweakdb/usedhashes.kark']
            with zipfile.ZipFile(path, 'w') as archive:
                for name, data in payload.items():
                    archive.writestr(name, data)
            item['sha256'] = c.sha(path)
            self.save()
            with self.assertRaisesRegex(ValueError, 'CET runtime payload'):
                verification.distribution(self.root)

    def test_stale_dependency_pin_is_rejected(self):
        self.lock['dependencies']['cet-luajit']['commit'] = 'new-pin'
        with self.assertRaisesRegex(ValueError, 'dependency/resource provenance'):
            verification.distribution(self.root)

    def test_resource_checksum_and_missing_license_are_rejected(self):
        original = self.asset.read_bytes()
        self.asset.write_bytes(b'changed asset')
        with self.assertRaisesRegex(ValueError, 'resource bundle checksum'):
            package.create('cyber-engine-tweaks', 'Release')
        self.asset.write_bytes(original)
        (self.root / 'build/deps/cet-freetype/docs/FTL.TXT').unlink()
        with self.assertRaises(FileNotFoundError):
            package.create('cyber-engine-tweaks', 'Release')


class CetAddressHashes(unittest.TestCase):
    def test_cet_ul_hashes_must_resolve_uniquely(self):
        with tempfile.TemporaryDirectory() as folder:
            root = pathlib.Path(folder)
            projects = {}
            for name in ['codeware', 'tweak-xl', 'archive-xl', 'cyber-engine-tweaks']:
                projects[name] = {'path': 'sources/' + name}
                library = 'src/reverse/Addresses.h' if name == 'cyber-engine-tweaks' else 'src/Red/Addresses/Library.hpp'
                path = root / projects[name]['path'] / library
                path.parent.mkdir(parents=True)
                if name == 'cyber-engine-tweaks':
                    text = 'constexpr uint32_t CGame_Main = 12345UL;'
                elif name == 'archive-xl':
                    text = ('constexpr uint32_t JournalTree_ProcessJournalIndex = 1;\n'
                            'constexpr uint32_t ResourceLoader_OnUpdate = 2;')
                else:
                    text = ''
                path.write_text(text)
            addresses = root / 'addresses.json'
            with patch.object(c, 'ROOT', root), patch.object(c, 'config', return_value={'projects': projects}), contextlib.redirect_stdout(io.StringIO()):
                for hashes in [[], ['12345', '12345']]:
                    addresses.write_text(json.dumps([{'hash': value} for value in hashes]))
                    with self.assertRaisesRegex(ValueError, 'cyber-engine-tweaks: unexpected missing/colliding'):
                        verification.addresses(addresses)
                addresses.write_text('[{"hash":"12345"}]')
                verification.addresses(addresses)


if __name__ == '__main__':
    unittest.main()
