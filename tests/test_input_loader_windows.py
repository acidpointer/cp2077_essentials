"""Opt-in DLL integration tests, run entirely in generated fake game directories.

Build tests/native/input-loader with our clang-cl toolchain, then set
CP2077_INPUT_HARNESS to its input-loader-lifecycle.exe. CP2077_INPUT_DLL defaults
to build/plugins/release/bin/input_loader.dll. Set CP2077_INPUT_BASE to a directory
of copied base XMLs to exercise real-data fixtures instead of synthetic XML.
On Linux use an isolated WINEPREFIX and CP2077_INPUT_RUNNER=wine. Wine is optional.
"""
import concurrent.futures
import os
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET


@unittest.skipUnless(os.environ.get('CP2077_INPUT_HARNESS'), 'optional Windows DLL harness')
class InputLoaderWindows(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = pathlib.Path(__file__).resolve().parents[1]
        cls.harness = pathlib.Path(os.environ['CP2077_INPUT_HARNESS']).resolve()
        cls.dll = pathlib.Path(os.environ.get('CP2077_INPUT_DLL', cls.root/'build/plugins/release/bin/input_loader.dll')).resolve()
        cls.runner = [os.environ['CP2077_INPUT_RUNNER']] if os.environ.get('CP2077_INPUT_RUNNER') else []
        cls.base = pathlib.Path(os.environ['CP2077_INPUT_BASE']) if os.environ.get('CP2077_INPUT_BASE') else None
        cls.neutral = (cls.root/'sources/input-loader/input_loader.ini').read_bytes()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cp2077-input-dll-')
        self.addCleanup(self.temporary.cleanup)
        self.fixture = pathlib.Path(self.temporary.name)
        for name in ['bin/x64', 'r6/config', 'r6/input', 'red4ext/plugins/input_loader',
                     'red4ext/logs', 'engine/config/platform/pc']:
            (self.fixture/name).mkdir(parents=True)
        shutil.copyfile(self.harness, self.fixture/'bin/x64/test.exe')
        shutil.copyfile(self.dll, self.fixture/'red4ext/plugins/input_loader/input_loader.dll')
        for name, data in {
            'inputContexts.xml': '<bindings><context name="Exploration"/></bindings>',
            'inputUserMappings.xml': '<bindings><mapping name="Jump_Button" type="Button"/></bindings>'
        }.items():
            path = self.fixture/'r6/config'/name
            if self.base:
                shutil.copyfile(self.base/name, path)
            else:
                path.write_text(data)
        (self.fixture/'r6/input/append.xml').write_text(
            '<bindings><context name="AuditContext" append="true"><action name="AuditAction" map="Jump_Button"/></context></bindings>')
        (self.fixture/'r6/input/інпут.XML').write_text(
            '<bindings><context name="UnicodePath"/></bindings>')
        (self.fixture/'r6/registered.xml').write_text(
            '<bindings><context name="LateContext"><action name="LateAction" map="LateMapping"/></context>'
            '<mapping name="LateMapping" type="Button"/></bindings>')
        self.ini = self.fixture/'engine/config/platform/pc/input_loader.ini'
        self.ini.write_bytes(self.neutral)

    def run_dll(self, mode):
        result = subprocess.run(self.runner + [str(self.fixture/'bin/x64/test.exe'), mode],
                                capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def selected_pair(self):
        text = self.ini.read_text()
        paths = re.findall(r'Input(?:Context|Mapping)File = "([^"]+)"', text)
        self.assertEqual(len(paths), 2, text)
        files = [self.fixture/'r6'/path.replace('\\', '/') for path in paths]
        self.assertEqual(files[0].parent, files[1].parent)
        pair = [ET.parse(path).getroot() for path in files]
        self.assertTrue(all(root.tag == 'bindings' for root in pair))
        return pair

    def test_startup_deduplication_lifecycle_and_log_failure(self):
        for mode in ['normal', 'duplicate-add', 'concurrent', 'stale-error', 'log-denied', 'seal', 'cancel']:
            with self.subTest(mode=mode):
                self.assertIn('MAIN=1 ENTER=1 EXIT=0', self.run_dll(mode))
                contexts, mappings = self.selected_pair()
                self.assertEqual(len(contexts.findall('./context[@name="AuditContext"]/action')), 1)
                self.assertEqual(len(contexts.findall('./context[@name="LateContext"]')), 0)
                self.assertEqual(len(mappings.findall('./mapping[@name="LateMapping"]')), 0)

    def test_preload_registration_and_immutable_baseline(self):
        for mode in ['pre-add', 'immutable-base']:
            with self.subTest(mode=mode):
                self.run_dll(mode)
                contexts, mappings = self.selected_pair()
                self.assertEqual(len(contexts.findall('./context[@name="LateContext"]')), 1)
                self.assertEqual(len(mappings.findall('./mapping[@name="LateMapping"]')), 1)

    def test_denied_selector_preserves_previous_pair(self):
        self.run_dll('selector-denied')
        contexts, mappings = self.selected_pair()
        self.assertFalse(contexts.findall('./context[@name="LateContext"]'))
        self.assertFalse(mappings.findall('./mapping[@name="LateMapping"]'))

    def test_failure_and_disabled_plugin_keep_neutral_installation(self):
        self.assertNotIn(b'InputContextFile', self.ini.read_bytes())  # DLL never executes
        for mode in ['null-sdk', 'callback-denied']:
            with self.subTest(mode=mode):
                self.run_dll(mode)
                self.assertEqual(self.ini.read_bytes(), self.neutral)

    def test_legacy_recovery_precedes_sdk_and_cache_errors(self):
        for mode in ['legacy-null', 'legacy-cache-fail']:
            with self.subTest(mode=mode):
                self.ini.write_text('[Player/Input]\nInputContextFile = "cache\\inputContexts.xml"\n'
                                    'InputMappingFile = "cache\\inputUserMappings.xml"\n')
                if mode == 'legacy-cache-fail':
                    (self.fixture/'r6/cache').write_text('not a directory')
                self.run_dll(mode)
                self.assertEqual(self.ini.read_bytes(), self.neutral)

    def test_base_scan_and_cache_failures_do_not_activate(self):
        for mode in ['empty-base', 'bad-base', 'multiple-roots', 'scan-fail', 'cache-dir-fail']:
            with self.subTest(mode=mode):
                # Separate fixture per failure, retaining this method's clean base.
                if mode in ['empty-base', 'bad-base', 'multiple-roots']:
                    data = {'empty-base': '<bindings/>', 'bad-base': '<bindings><broken>',
                            'multiple-roots': '<bindings/><other/>'}[mode]
                    path = self.fixture/'r6/config/inputContexts.xml'
                    original = path.read_bytes(); path.write_text(data)
                elif mode == 'scan-fail':
                    shutil.rmtree(self.fixture/'r6/input'); (self.fixture/'r6/input').write_text('not a directory')
                else:
                    (self.fixture/'r6/cache').write_text('not a directory')
                self.run_dll(mode)
                self.assertEqual(self.ini.read_bytes(), self.neutral)
                if mode in ['empty-base', 'bad-base', 'multiple-roots']:
                    path.write_bytes(original)
                elif mode == 'scan-fail':
                    (self.fixture/'r6/input').unlink(); (self.fixture/'r6/input').mkdir()

    def test_custom_ini_is_preserved(self):
        custom = b'[Player/Input]\nCustom=value\n'; self.ini.write_bytes(custom)
        self.run_dll('custom')
        self.assertEqual(self.ini.read_bytes(), custom)

    def test_malformed_mod_is_rejected_whole(self):
        (self.fixture/'r6/input/append.xml').write_text(
            '<bindings><context name="PARTIAL"/><broken></bindings>')
        self.run_dll('normal')
        contexts, _ = self.selected_pair()
        self.assertFalse(contexts.findall('./context[@name="PARTIAL"]'))
        self.assertTrue(contexts.findall('./context[@name="Exploration"]'))

    def test_file_aliases_and_directory_links(self):
        alias = self.fixture/'r6/input/alias.XML'
        alias.symlink_to(self.fixture/'r6/input/append.xml')
        os.link(self.fixture/'r6/input/append.xml', self.fixture/'r6/input/hardlink.xml')
        outside = self.fixture/'outside'; outside.mkdir()
        (outside/'ignored.xml').write_text('<bindings><context name="IgnoredLink"/></bindings>')
        (self.fixture/'r6/input/directory-link').symlink_to(outside, target_is_directory=True)
        self.run_dll('aliases')
        contexts, _ = self.selected_pair()
        self.assertEqual(len(contexts.findall('./context[@name="AuditContext"]/action')), 1)
        self.assertEqual(len(contexts.findall('./context[@name="UnicodePath"]')), 1)
        self.assertFalse(contexts.findall('./context[@name="IgnoredLink"]'))

    def test_cross_process_publication(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(self.run_dll, ['normal'] * 4))
        self.assertEqual(len(results), 4)
        contexts, _ = self.selected_pair()
        self.assertEqual(len(contexts.findall('./context[@name="AuditContext"]/action')), 1)


if __name__ == '__main__':
    unittest.main()
