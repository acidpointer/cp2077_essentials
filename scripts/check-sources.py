#!/usr/bin/env python3
"""Audit tracked source boundaries and official upstream submodule pins (read-only)."""
import pathlib

import common as c


UPSTREAMS = {
    'red4ext': 'https://github.com/wopss/RED4ext.git',
    'archive-xl': 'https://github.com/psiberx/cp2077-archive-xl.git',
    'codeware': 'https://github.com/psiberx/cp2077-codeware.git',
    'tweak-xl': 'https://github.com/psiberx/cp2077-tweak-xl.git',
    'input-loader': 'https://github.com/jackhumbert/cyberpunk2077-input-loader.git',
    'red4ext-sdk': 'https://github.com/wopss/RED4ext.SDK.git',
    'cyber-engine-tweaks': 'https://github.com/maximegmd/CyberEngineTweaks.git',
}
ROOT_FILES = {'.gitattributes', '.gitignore', '.gitmodules', '.vscode/settings.json', 'AGENTS.md',
              'CMakeLists.txt', 'README.md', 'build-linux.sh', 'sources.lock.json'}
ROOT_DIRECTORIES = {'.github', 'cmake', 'docs', 'licenses', 'patches', 'scripts', 'tests'}


def audit():
    projects = c.config()['projects']
    if set(projects) != set(UPSTREAMS):
        raise ValueError('Source projects differ from the reviewed official upstream list.')
    pins = {}
    for record in c.git(c.ROOT, '--no-optional-locks', 'ls-files', '--stage', '-z').stdout.split(b'\0'):
        if not record:
            continue
        header, raw_path = record.split(b'\t', 1)
        mode, commit, stage = header.decode().split()
        path = raw_path.decode()
        if stage != '0':
            raise ValueError(f'{path}: unresolved index entry.')
        if mode == '160000':
            pins[path] = commit
        elif path not in ROOT_FILES and pathlib.PurePosixPath(path).parts[0] not in ROOT_DIRECTORIES:
            raise ValueError(f'{path}: outside reviewed root-owned directories; do not vendor upstream sources.')
    expected = {'sources/' + name for name in UPSTREAMS}
    if set(pins) != expected:
        raise ValueError('Expected exactly seven source Gitlinks; copied source trees are forbidden.')
    registered = c.git(c.ROOT, 'config', '--file', '.gitmodules', '--get-regexp', r'^submodule\..*\.path$').stdout.decode().splitlines()
    if set(registered) != {f'submodule.{path}.path {path}' for path in expected}:
        raise ValueError('.gitmodules must register exactly the seven source projects.')
    for name, url in UPSTREAMS.items():
        path = projects[name]['path']
        if path != 'sources/' + name:
            raise ValueError(f'{name}: unexpected source path.')
        configured = c.git(c.ROOT, 'config', '--file', '.gitmodules', '--get', f'submodule.{path}.url').stdout.decode().strip()
        if configured != url:
            raise ValueError(f'{name}: expected official upstream URL {url}.')
        source = c.ROOT / path
        if not (source / '.git').is_file():
            raise ValueError(f'{name}: initialize the official registered submodule; do not copy sources.')
        head = c.git(source, 'rev-parse', 'HEAD').stdout.decode().strip()
        if head != pins[path]:
            raise ValueError(f'{name}: HEAD differs from the root Gitlink.')
        print(f'{name}: official submodule at {head[:10]}')
    print('Tracked source boundaries passed; compatibility changes belong in patches/.')


if __name__ == '__main__':
    c.entry(audit)
