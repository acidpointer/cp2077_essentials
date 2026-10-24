"""Optional Microsoft-ABI regressions; no game DLLs are loaded.

Build tests/native/load-freeze with cmake/toolchains/clang-cl-linux.cmake.
Set CP2077_FREEZE_TEST_DIR to its binary directory. On Linux also set
CP2077_FREEZE_RUNNER=wine and use an isolated WINEPREFIX. The optional old SDK
overlay target supplies negative controls for the four incorrect vtable slots.
"""
import os
from pathlib import Path
import shlex
import subprocess
import unittest


@unittest.skipUnless(os.environ.get('CP2077_FREEZE_TEST_DIR'),
                     'set CP2077_FREEZE_TEST_DIR to the native fixture build')
class LoadFreezeTests(unittest.TestCase):
    def run_fixture(self, name, *arguments, expected=0, marker='PASS:'):
        executable = Path(os.environ['CP2077_FREEZE_TEST_DIR']) / (name + '.exe')
        self.assertTrue(executable.is_file(), str(executable))
        runner = shlex.split(os.environ.get('CP2077_FREEZE_RUNNER', ''))
        result = subprocess.run([*runner, str(executable.resolve()), *arguments],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors='replace', timeout=45)
        self.assertEqual(result.returncode, expected, result.stdout)
        self.assertIn(marker, result.stdout)

    def test_retail_abi(self):
        self.run_fixture('sdk_transaction_system_abi')

    def test_lock_routing_and_hook_lifecycle(self):
        self.run_fixture('archive_attachment_locking')

    def test_listener_lifetime_and_concurrency(self):
        self.run_fixture('archive_puppet_listener_lifetime')

    def test_old_locking_path_is_rejected(self):
        self.run_fixture('archive_attachment_locking', 'old-reacquisition', expected=42,
                         marker='self-deadlock: locking lookup under exclusive ownership')

    def test_old_sdk_dispatch_is_rejected(self):
        old = Path(os.environ['CP2077_FREEZE_TEST_DIR']) / 'sdk_transaction_system_abi_old.exe'
        if not old.is_file():
            self.skipTest('build with OLD_SDK_OVERLAY for the negative ABI controls')
        for method in ('register', 'unregister', 'item-id', 'item-handle'):
            with self.subTest(method=method):
                self.run_fixture('sdk_transaction_system_abi_old', method, expected=42, marker='FAIL:')


if __name__ == '__main__':
    unittest.main()
