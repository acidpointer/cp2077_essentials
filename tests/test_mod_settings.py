"""Mod Settings package preservation, dependency overlays and optional native ABI checks."""
import contextlib
import io
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import build as build_driver
import mod_settings
import package
import verification


class WorkflowBuild(unittest.TestCase):
    def test_shared_release_command_builds_and_packages_every_runtime_mod(self):
        workflow = (c.ROOT / '.github/workflows/release.yml').read_text()
        commands = re.findall(r'^\s+run: (\./build-linux\.sh[^\n]*)$', workflow, re.M)
        self.assertEqual(len(commands), 1, 'Release must use the shared build entry point')
        command = shlex.split(commands[0])
        lock = c.config()
        expected = set(lock['projects']) - {'red4ext-sdk'}
        invocations = []
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with contextlib.ExitStack() as stack:
                for mocked in [patch.object(c, 'ROOT', root), patch.object(c, 'config', return_value=lock),
                               patch.object(sys, 'argv', command),
                               patch.object(build_driver.bootstrap, 'sources'),
                               patch.object(build_driver.bootstrap, 'dependencies'),
                               patch.object(build_driver.toolchain, 'ensure', return_value=root / 'xwin'),
                               patch.object(build_driver.patches, 'identity', return_value={}),
                               patch.object(mod_settings, 'prepare'),
                               patch.object(package, 'cet_dependencies', return_value={}),
                               patch.object(package, 'mod_settings_dependencies', return_value={}),
                               patch.object(build_driver, 'logged', side_effect=lambda args, log: invocations.append(args))]:
                    stack.enter_context(mocked)
                create = stack.enter_context(patch.object(package, 'create'))
                collect = stack.enter_context(patch.object(build_driver.distribution, 'collect', return_value=root / 'dist/release'))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                build_driver.main()
        self.assertEqual({call.args[0] for call in create.call_args_list}, expected)
        self.assertEqual(len(create.call_args_list), len(expected))
        self.assertTrue(all(call.args[1] == 'Release' for call in create.call_args_list))
        targets = [args[args.index('--target') + 1] for args in invocations if '--target' in args]
        self.assertIn('ModSettings', targets)
        self.assertEqual(set(targets), {'all'} | {c.NAMES[name] for name in expected - {'red4ext'}})
        self.assertEqual(set(collect.call_args.args[1]), expected)


class ModSettingsPayload(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'sources/mod-settings'
        self.build = self.root / 'build/plugins/release'
        inputs = {
            self.build / 'bin/mod_settings.dll': b'rebuilt DLL',
            self.build / 'bin/mod_settings.pdb': b'symbols',
            self.source / 'readme.md': b'upstream readme',
            self.source / 'license.md': b'upstream license',
            self.source / 'src/wolvenkit/packed/archive/pc/mod/ModSettings.archive': b'packed ink widgets',
            self.source / 'src/archiveXL/ModSettings.archive.xl': b'localization: en-us',
            self.source / 'src/redscript/Module.reds.in': b'module ModSettingsModule\n// @PROJECT_VERSION_MAJOR@.@PROJECT_VERSION_MINOR@.@PROJECT_VERSION_PATCH@\n',
            self.source / 'src/redscript/one.reds': b'// comments retained\n@addMethod(PlayerPuppet)\npublic func First() {}\n',
            self.source / 'src/redscript/sub/two.reds': b'public func Second() {}\n',
            self.source / 'deps/red_lib/LICENSE': b'RedLib license',
            self.source / 'deps/red_lib/vendor/nameof/LICENSE': b'nameof license',
            self.root / 'sources/red4ext-sdk/LICENSE.md': b'SDK license',
            self.root / 'sources/archive-xl/LICENSE': b'ArchiveXL license',
            self.source / 'user.ini': b'existing user settings must not ship',
            self.source / 'build/mod_settings.dll': b'old prebuilt DLL must not ship',
        }
        for path, data in inputs.items():
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        lock = {'projects': {'mod-settings': {'path': 'sources/mod-settings', 'version': '0.2.21'}}}
        for mocked in [patch.object(c, 'ROOT', self.root), patch.object(c, 'config', return_value=lock),
                       patch.object(package.patches, 'identity', return_value={}),
                       patch.object(package, 'mod_settings_dependencies', return_value={'redlib': 'fixture'}),
                       patch.object(verification, 'binary'),
                       patch.object(verification, 'exports', return_value={'Main', 'Query', 'Supports', 'AddVariable'})]:
            mocked.start()
            self.addCleanup(mocked.stop)
        with contextlib.redirect_stdout(io.StringIO()):
            self.record = package.create('mod-settings', 'Release')
        self.manifest = {'schema': 2, 'config': 'Release', 'sdk': {}, 'projects': {'mod-settings': self.record}}
        self.save()

    def save(self):
        (self.root / 'manifest.json').write_text(json.dumps(self.manifest))

    def test_complete_runtime_and_separate_symbols(self):
        item = self.record['files']['runtime']
        self.assertTrue(item['file'].endswith('ModSettings-0.2.21-game2.5.zip'))
        with zipfile.ZipFile(self.root / item['file']) as archive:
            payload = {name: archive.read(name) for name in archive.namelist()}
        prefix = 'red4ext/plugins/mod_settings/'
        self.assertEqual(payload[prefix + 'mod_settings.dll'], b'rebuilt DLL')
        self.assertIn(b'@addMethod(PlayerPuppet)', payload[prefix + 'packed.reds'])
        self.assertIn(b'// comments retained', payload[prefix + 'packed.reds'])
        self.assertIn(b'0.2.21', payload[prefix + 'module.reds'])
        self.assertEqual(payload[prefix + 'ModSettings.archive'], b'packed ink widgets')
        self.assertFalse(any(name.endswith(('.ini', '.pdb', '.asi')) for name in payload))
        self.assertEqual(len([name for name in payload if '/licenses/' in name]), 4)
        with zipfile.ZipFile(self.root / self.record['files']['symbols']['file']) as archive:
            self.assertEqual(archive.namelist(), [prefix + 'mod_settings.pdb'])
        verification.distribution(self.root)

    def test_missing_or_changed_assets_scripts_and_notices_are_rejected(self):
        item = self.record['files']['runtime']
        path = self.root / item['file']
        with zipfile.ZipFile(path) as archive:
            original = {name: archive.read(name) for name in archive.namelist()}
        for tail in ['ModSettings.archive', 'ModSettings.archive.xl', 'packed.reds', 'module.reds', 'licenses/RedLib.txt']:
            with self.subTest(tail=tail):
                changed = dict(original)
                changed.pop('red4ext/plugins/mod_settings/' + tail)
                with zipfile.ZipFile(path, 'w') as archive:
                    for name, data in changed.items():
                        archive.writestr(name, data)
                item['sha256'] = c.sha(path)
                self.save()
                with self.assertRaisesRegex(ValueError, 'runtime payload'):
                    verification.distribution(self.root)

    def test_stale_dependency_and_missing_native_api_are_rejected(self):
        with patch.object(package, 'mod_settings_dependencies', return_value={'redlib': 'changed'}):
            with self.assertRaisesRegex(ValueError, 'stale dependency'):
                verification.distribution(self.root)
        with patch.object(verification, 'exports', return_value={'Main', 'Query', 'Supports'}):
            with self.assertRaisesRegex(ValueError, 'AddVariable'):
                verification.distribution(self.root)


class RedLibOverlay(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'sources/mod-settings/deps/red_lib'
        self.header = self.source / 'include/test.hpp'
        self.header.parent.mkdir(parents=True)
        self.header.write_text('old SDK API\n')
        self.layer = self.root / 'patches/mod-settings-redlib/001-sdk-api.patch'
        self.layer.parent.mkdir(parents=True)
        self.layer.write_text('--- a/include/test.hpp\n+++ b/include/test.hpp\n@@ -1 +1 @@\n-old SDK API\n+shared SDK API\n')
        self.lock = {'mod_settings_redlib': {'path': 'sources/mod-settings/deps/red_lib', 'commit': 'abc',
                     'patches': [{'path': self.layer.relative_to(self.root).as_posix(), 'sha256': c.sha(self.layer)}]}}
        (self.root / 'sources.lock.json').write_text(json.dumps(self.lock))
        def git(source, *args, **kwargs):
            if 'rev-parse' in args: return SimpleNamespace(stdout=b'abc\n')
            if 'ls-files' in args: return SimpleNamespace(stdout=b'include/test.hpp\n')
            if 'status' in args: return SimpleNamespace(stdout=b'')
            self.fail('Unexpected dependency Git mutation: ' + repr(args))
        for mocked in [patch.object(c, 'ROOT', self.root), patch.object(c, 'git', side_effect=git)]:
            mocked.start()
            self.addCleanup(mocked.stop)

    def test_overlay_reconstruction_and_export_leave_dependency_unchanged(self):
        generated = mod_settings.prepare() / 'include/test.hpp'
        self.assertEqual(generated.read_text(), 'shared SDK API\n')
        generated.write_text('reviewed SDK change\n')
        mod_settings.export()
        self.assertEqual(self.header.read_text(), 'old SDK API\n')
        self.assertEqual(c.config()['mod_settings_redlib']['patches'][0]['sha256'], c.sha(self.layer))
        mod_settings.prepare()
        self.assertEqual(generated.read_text(), 'reviewed SDK change\n')

    def test_bad_checksum_preserves_old_overlay(self):
        generated = mod_settings.prepare() / 'include/test.hpp'
        before = generated.read_bytes()
        self.layer.write_text('corrupt patch')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            mod_settings.prepare()
        self.assertEqual(generated.read_bytes(), before)

    def test_unexported_overlay_edits_are_preserved(self):
        generated = mod_settings.prepare() / 'include/test.hpp'
        generated.write_text('work in progress\n')
        with self.assertRaisesRegex(ValueError, 'Unexported RedLib overlay'):
            mod_settings.prepare()
        self.assertEqual(generated.read_text(), 'work in progress\n')
        with self.assertRaisesRegex(ValueError, 'Unexported RedLib overlay'):
            mod_settings.check()

    def test_dependency_edits_and_unexpected_files_are_rejected(self):
        with patch.object(c, 'git', return_value=SimpleNamespace(stdout=b'wrong-head\n')):
            with self.assertRaisesRegex(ValueError, 'pin differs'):
                mod_settings.prepare()
        generated = mod_settings.prepare()
        (generated / 'surprise.hpp').write_text('not pinned')
        with self.assertRaisesRegex(ValueError, 'Unexpected generated'):
            mod_settings.prepare()
        with self.assertRaisesRegex(ValueError, 'existing pinned'):
            mod_settings.export()


@unittest.skipUnless(os.environ.get('CP2077_MOD_SETTINGS_HARNESS'), 'set CP2077_MOD_SETTINGS_HARNESS for the native fixture')
class NativeRuntime(unittest.TestCase):
    def test_declarative_discovery_retail_callbacks_and_hook_rollback(self):
        runner = shlex.split(os.environ.get('CP2077_MOD_SETTINGS_RUNNER', ''))
        result = subprocess.run([*runner, os.environ['CP2077_MOD_SETTINGS_HARNESS']],
                                capture_output=True, text=True, timeout=45)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS:', result.stdout)


if __name__ == '__main__':
    unittest.main()
