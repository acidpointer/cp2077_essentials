"""Publication retries must handle existing releases without hiding API failures."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import publishing


class ReleasePublication(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cp2077-publishing-tests-')
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        directory = self.root / 'build/github-release'
        directory.mkdir(parents=True)
        files = {'mod.zip': b'archive fixture', 'SHA256SUMS': b'checksum fixture',
                 'manifest.json': json.dumps({'essentials_release': 'v0.0.2'}).encode()}
        for name, data in files.items():
            (directory / name).write_bytes(data)
        (directory / 'assets.txt').write_text(''.join('build/github-release/' + name + '\n' for name in files))
        (directory / 'notes.md').write_text('Prepared release notes.')
        self.digests = {name: 'sha256:' + c.sha(directory / name) for name in files}
        self.existing = {'tag_name': 'v0.0.2', 'draft': False, 'immutable': False,
                         'prerelease': True, 'body': 'Maintainer notes', 'assets': []}
        self.query_error = None
        self.upload_error = False
        self.commands = []
        for mocked in [patch.object(c, 'ROOT', self.root), patch.object(c, 'run', side_effect=self.run_command)]:
            mocked.start()
            self.addCleanup(mocked.stop)

    def run_command(self, command, **kwargs):
        self.commands.append([str(value) for value in command])
        if command[1] == 'api':
            if self.query_error:
                return subprocess.CompletedProcess(command, 1, stdout=b'', stderr=self.query_error)
            return subprocess.CompletedProcess(command, 0, stdout=json.dumps(self.existing).encode(), stderr=b'')
        if command[1:3] == ['release', 'upload'] and self.upload_error:
            raise RuntimeError('upload failed')
        return subprocess.CompletedProcess(command, 0, stdout=b'', stderr=b'')

    def publish(self):
        publishing.publish('v0.0.2', 'owner/essentials')

    def matching_assets(self):
        self.existing['assets'] = [{'name': name, 'digest': digest, 'state': 'uploaded'}
                                   for name, digest in self.digests.items()]

    def test_existing_empty_release_gets_assets_without_recreation(self):
        self.publish()
        self.assertEqual([command[1:3] for command in self.commands[1:]], [['release', 'upload']])
        self.assertIn('--clobber', self.commands[-1])

    def test_partial_release_uploads_complete_prepared_set(self):
        self.existing['assets'] = [{'name': 'mod.zip', 'digest': 'sha256:old', 'state': 'uploaded'}]
        self.publish()
        for name in self.digests:
            self.assertIn('build/github-release/' + name, self.commands[-1])

    def test_complete_published_release_is_a_no_op(self):
        self.matching_assets()
        self.publish()
        self.assertEqual(len(self.commands), 1)

    def test_draft_published_only_after_successful_upload(self):
        self.existing['draft'] = True
        self.publish()
        self.assertEqual([command[1:3] for command in self.commands[1:]],
                         [['release', 'upload'], ['release', 'edit']])
        self.assertIn('--draft=false', self.commands[-1])
        self.commands.clear()
        self.upload_error = True
        with self.assertRaisesRegex(RuntimeError, 'upload failed'):
            self.publish()
        self.assertFalse(any(command[1:3] == ['release', 'edit'] for command in self.commands))

    def test_immutable_release_is_verified_or_refused_without_mutation(self):
        self.existing['immutable'] = True
        with self.assertRaisesRegex(ValueError, 'immutable'):
            self.publish()
        self.assertEqual(len(self.commands), 1)
        self.commands.clear()
        self.matching_assets()
        self.publish()
        self.assertEqual(len(self.commands), 1)

    def test_new_release_created_only_for_404(self):
        self.query_error = b'gh: Not Found (HTTP 404)'
        self.publish()
        self.assertEqual(self.commands[-1][1:3], ['release', 'create'])
        self.assertIn('--verify-tag', self.commands[-1])
        self.assertNotIn('--prerelease', self.commands[-1])
        for failure in [b'gh: Forbidden (HTTP 403)', b'connection timed out', b'gh: Bad Gateway (HTTP 502)']:
            self.commands.clear()
            self.query_error = failure
            with self.assertRaisesRegex(RuntimeError, 'Cannot inspect'):
                self.publish()
            self.assertEqual(len(self.commands), 1)

    def test_rc_new_release_is_a_prerelease(self):
        manifest = self.root / 'build/github-release/manifest.json'
        manifest.write_text(json.dumps({'essentials_release': 'v0.0.2-rc.1'}))
        self.query_error = b'gh: Not Found (HTTP 404)'
        publishing.publish('v0.0.2-rc.1', 'owner/essentials')
        self.assertIn('--prerelease', self.commands[-1])

    def test_missing_asset_or_wrong_tag_stops_before_api_call(self):
        with self.assertRaisesRegex(ValueError, 'different tag'):
            publishing.publish('v0.0.3', 'owner/essentials')
        (self.root / 'build/github-release/mod.zip').unlink()
        with self.assertRaisesRegex(ValueError, 'Invalid prepared'):
            self.publish()
        self.assertEqual(self.commands, [])


if __name__ == '__main__':
    unittest.main()
