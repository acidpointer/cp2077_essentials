"""Integration checks for cumulative patches using real disposable Git submodules."""
import contextlib
import hashlib
import io
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import patches
import bootstrap


def git(path, *args):
    result = subprocess.run(['git', '-C', str(path), *args], capture_output=True)
    if result.returncode:
        raise AssertionError(result.stderr.decode())
    return result.stdout.decode().strip()


class PatchWorkflow(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cp2077-patch-tests-')
        self.directory = pathlib.Path(self.temporary.name)
        self.root = self.directory / 'root'
        self.upstream = self.directory / 'upstream'
        self.root.mkdir(); self.upstream.mkdir()
        for repo in [self.root, self.upstream]:
            git(repo, 'init', '-q')
            git(repo, 'config', 'user.name', 'Patch tests')
            git(repo, 'config', 'user.email', 'patch-tests@example.invalid')
        for name, text in {'a.txt': 'base\n', 'delete.txt': 'delete me\n', 'rename.txt': 'rename me\n' * 20, '.gitignore': 'ignored.log\nbuild/\n'}.items():
            (self.upstream / name).write_text(text)
        git(self.upstream, 'add', '.')
        git(self.upstream, 'commit', '-qm', 'test baseline')
        git(self.root, '-c', 'protocol.file.allow=always', 'submodule', 'add', str(self.upstream), 'sources/codeware')
        self.source = self.root / 'sources/codeware'
        self.base = git(self.source, 'rev-parse', 'HEAD')
        self.previous_root = c.ROOT; c.ROOT = self.root
        self.patch = self.root / 'patches/codeware/001-essentials.patch'
        self.patch.parent.mkdir(parents=True)
        (self.source / 'a.txt').write_text('patched\n')
        data = patches.diff(self.source, self.base, patches.snapshot(self.source, self.base))
        self.patch.write_bytes(data)
        self.lock = {'projects': {'codeware': {'path': 'sources/codeware', 'patches': [{'path': 'patches/codeware/001-essentials.patch', 'sha256': hashlib.sha256(data).hexdigest()}]}}}
        c.atomic_json(self.root / 'sources.lock.json', self.lock)
        (self.source / 'a.txt').write_text('base\n')

    def tearDown(self):
        c.ROOT = self.previous_root
        self.temporary.cleanup()

    def call(self, function):
        with contextlib.redirect_stdout(io.StringIO()):
            function(['codeware'])

    def index_digest(self):
        path = pathlib.Path(git(self.source, 'rev-parse', '--git-path', 'index'))
        if not path.is_absolute():path = self.source / path
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def test_real_gitfile_apply_idempotency_and_index(self):
        self.assertTrue((self.source / '.git').is_file())
        before = self.index_digest()
        self.call(patches.apply); self.call(patches.apply); self.call(patches.check)
        self.assertEqual((self.source / 'a.txt').read_text(), 'patched\n')
        self.assertEqual(self.index_digest(), before)

    def test_local_edits_survive_and_export(self):
        self.call(patches.apply)
        (self.source / 'a.txt').write_text('developer edit\n')
        self.call(patches.apply)
        self.assertEqual((self.source / 'a.txt').read_text(), 'developer edit\n')
        with self.assertRaises(ValueError):self.call(patches.check)
        self.call(patches.export); self.call(patches.check)

    def test_export_new_binary_rename_delete_mode_and_staged_changes(self):
        self.call(patches.apply)
        (self.source / 'a.txt').write_text('staged\n'); git(self.source, 'add', 'a.txt')
        (self.source / 'a.txt').write_text('latest unstaged\n')
        (self.source / 'new.sh').write_text('#!/bin/sh\necho test\n'); (self.source / 'new.sh').chmod(0o755)
        (self.source / 'new.bin').write_bytes(bytes(range(256)) * 3)
        (self.source / 'delete.txt').unlink()
        (self.source / 'rename.txt').rename(self.source / 'renamed.txt')
        (self.source / 'ignored.log').write_text('must not be exported\n')
        before = self.index_digest()
        self.call(patches.export); self.call(patches.check)
        self.assertEqual(self.index_digest(), before)
        data = self.patch.read_bytes()
        for expected in [b'new file mode 100755', b'GIT binary patch', b'rename from rename.txt', b'deleted file mode', b'latest unstaged']:
            self.assertIn(expected, data)
        self.assertNotIn(b'ignored.log', data)

    def test_conflict_does_not_touch_files(self):
        (self.source / 'a.txt').write_text('conflicting local edit\n')
        before = (self.source / 'a.txt').read_bytes()
        with self.assertRaises(ValueError):self.call(patches.apply)
        self.assertEqual((self.source / 'a.txt').read_bytes(), before)

    def test_checksum_failure(self):
        self.patch.write_bytes(self.patch.read_bytes() + b'corruption')
        with self.assertRaises(ValueError):self.call(patches.apply)
        self.assertEqual((self.source / 'a.txt').read_text(), 'base\n')

    def test_wrong_gitlink_base(self):
        git(self.source, 'config', 'user.name', 'Patch tests'); git(self.source, 'config', 'user.email', 'patch-tests@example.invalid')
        (self.source / 'other').write_text('new commit\n')
        git(self.source, 'add', 'other'); git(self.source, 'commit', '-qm', 'different base')
        changed_head = git(self.source, 'rev-parse', 'HEAD')
        with self.assertRaises(ValueError):self.call(patches.apply)
        with self.assertRaises(ValueError):bootstrap.sources()
        self.assertEqual(git(self.source, 'rev-parse', 'HEAD'), changed_head)
        self.assertEqual((self.source / 'other').read_text(), 'new commit\n')

    def test_unapply_preserves_unsaved_edits(self):
        self.call(patches.apply)
        (self.source / 'a.txt').write_text('unsaved\n')
        with self.assertRaises(ValueError):self.call(patches.unapply)
        self.assertEqual((self.source / 'a.txt').read_text(), 'unsaved\n')
        self.call(patches.export); self.call(patches.unapply); self.call(patches.unapply)
        self.assertEqual((self.source / 'a.txt').read_text(), 'base\n')
        self.call(patches.apply); self.call(patches.check)

    def test_refresh_only_known_saved_content(self):
        self.call(patches.apply)
        (self.source / 'a.txt').write_text('new saved patch\n')
        data = patches.diff(self.source, self.base, patches.snapshot(self.source, self.base))
        self.patch.write_bytes(data)
        self.lock['projects']['codeware']['patches'][0]['sha256'] = hashlib.sha256(data).hexdigest()
        c.atomic_json(self.root / 'sources.lock.json', self.lock)
        (self.source / 'a.txt').write_text('patched\n')
        self.call(patches.apply)
        self.assertEqual((self.source / 'a.txt').read_text(), 'new saved patch\n')
        self.call(patches.check)

    def test_empty_export_does_not_erase_patch(self):
        before = self.patch.read_bytes()
        with self.assertRaises(ValueError):self.call(patches.export)
        self.assertEqual(self.patch.read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
