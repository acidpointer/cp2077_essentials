"""Distribution publication must preserve the old release on failed verification."""
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'scripts'))
import common as c
import distribution


class DistributionPublication(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='cp2077-dist-tests-')
        self.root = pathlib.Path(self.temporary.name)
        self.previous_root = c.ROOT; c.ROOT = self.root
        self.dest = self.root / 'dist/release'; self.dest.mkdir(parents=True)
        (self.dest / 'original.zip').write_bytes(b'previous verified release')
        (self.dest / 'manifest.json').write_text(json.dumps({'schema': 2, 'projects': {}}))
        self.before = {p.name: p.read_bytes() for p in self.dest.iterdir()}
        output = self.root / 'build/packages/release/codeware'; output.mkdir(parents=True)
        archive = output / 'new.zip'; archive.write_bytes(b'candidate package')
        self.sdk = {'fingerprint': 'sdk'}; self.source = {'fingerprint': 'source'}
        self.record = {'project': 'Codeware', 'config': 'Release', 'source': self.source,
                       'sdk': self.sdk, 'files': {'runtime': {'file': archive.relative_to(self.root).as_posix(),
                       'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}}}
        self.metadata = output / 'package.json'; self.metadata.write_text(json.dumps(self.record))
        self.identity = patch.object(distribution.patches, 'identity', side_effect=lambda name: self.sdk if name == 'red4ext-sdk' else self.source)
        self.identity.start()

    def tearDown(self):
        self.identity.stop(); c.ROOT = self.previous_root; self.temporary.cleanup()

    def test_failed_candidate_verification_preserves_previous_release(self):
        with patch.object(distribution.verification, 'distribution', side_effect=ValueError('invalid candidate')):
            with self.assertRaises(ValueError):distribution.collect('Release', ['codeware'])
        self.assertEqual({p.name:p.read_bytes() for p in self.dest.iterdir()}, self.before)

    def test_stale_sdk_package_preserves_previous_release(self):
        self.record['sdk'] = {'fingerprint': 'wrong-sdk'}
        self.metadata.write_text(json.dumps(self.record))
        with self.assertRaises(ValueError):distribution.collect('Release', ['codeware'])
        self.assertEqual({p.name:p.read_bytes() for p in self.dest.iterdir()}, self.before)


if __name__ == '__main__':
    unittest.main()
