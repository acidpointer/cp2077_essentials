#!/usr/bin/env python3
"""Publish prepared assets, including retries against existing GitHub releases."""
import argparse
import json
import os
import re

import common as c


def publish(tag, repository):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-rc(?:\.[0-9]+)?)?', tag):
        raise ValueError('Use vX.Y.Z, vX.Y.Z-rc or vX.Y.Z-rc.N.')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repository):
        raise ValueError('Provide a GitHub repository as OWNER/REPO.')
    directory = c.ROOT / 'build/github-release'
    assets = []
    digests = {}
    for name in (directory / 'assets.txt').read_text().splitlines():
        path = c.ROOT / name
        if not path.is_file() or path.resolve().parent != directory.resolve():
            raise ValueError(f'Invalid prepared release asset: {name}')
        if path.name in digests:
            raise ValueError(f'Duplicate prepared release asset: {path.name}')
        assets.append(name)
        digests[path.name] = 'sha256:' + c.sha(path)
    if not assets:
        raise ValueError('The prepared release asset list is empty.')
    manifest = json.loads((directory / 'manifest.json').read_text())
    if manifest.get('essentials_release') != tag:
        raise ValueError('Prepared release assets belong to a different tag.')
    notes = directory / 'notes.md'
    if not notes.is_file():
        raise ValueError('Prepared release notes are missing.')

    # Only HTTP 404 means absent; authentication and network errors must stop here.
    query = c.run(['gh', 'api', f'repos/{repository}/releases/tags/{tag}'], check=False)
    if query.returncode:
        if not re.search(rb'\bHTTP 404\b', query.stderr):
            raise RuntimeError('Cannot inspect GitHub release: ' + query.stderr.decode(errors='replace'))
        command = ['gh', 'release', 'create', tag, *assets, '--repo', repository,
                   '--verify-tag', '--title', 'Cyberpunk 2077 Essentials ' + tag,
                   '--notes-file', notes, '--generate-notes']
        if '-rc' in tag:
            command.append('--prerelease')
        c.run(command)
        print(f'Created {repository} release {tag} with {len(assets)} assets.')
        return

    existing = json.loads(query.stdout)
    if existing.get('tag_name') != tag:
        raise ValueError('GitHub returned a different release tag.')
    remote = {asset['name']: asset.get('digest') for asset in existing.get('assets', [])
              if asset.get('state') == 'uploaded'}
    complete = all(remote.get(name) == digest for name, digest in digests.items())
    if complete and not existing.get('draft'):
        print(f'{tag}: all {len(assets)} prepared assets already exist with matching SHA256; nothing to upload.')
        return
    if existing.get('immutable'):
        raise ValueError(f'{tag}: release is immutable and its assets differ. Publish a new version tag.')
    if not complete:
        # Replace only prepared filenames, retaining unrelated assets and author notes.
        c.run(['gh', 'release', 'upload', tag, *assets, '--repo', repository, '--clobber'])
    if existing.get('draft'):
        c.run(['gh', 'release', 'edit', tag, '--repo', repository, '--draft=false'])
    print(f'{tag}: prepared assets attached to the existing release; its title, notes and prerelease setting are preserved.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    parser.add_argument('--repo', default=os.environ.get('GITHUB_REPOSITORY', ''))
    args = parser.parse_args()
    publish(args.tag, args.repo)


if __name__ == '__main__':
    c.entry(main)
