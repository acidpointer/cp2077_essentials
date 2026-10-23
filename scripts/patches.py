#!/usr/bin/env python3
"""Apply, export, inspect, or independently check cumulative submodule patches."""
import argparse
import contextlib
import hashlib
import json
import os
import pathlib
import tempfile

import common as c


@contextlib.contextmanager
def index(repo, base):
    with tempfile.TemporaryDirectory(prefix='cp2077-index-') as directory:
        env = dict(os.environ, GIT_INDEX_FILE=str(pathlib.Path(directory) / 'index'))
        c.git(repo, 'read-tree', base, env=env)
        yield env


def gitlinks(repo, base):
    links = {}
    for record in c.git(repo, 'ls-tree', '-r', '-z', base).stdout.split(b'\0'):
        if not record:
            continue
        header, path = record.split(b'\t', 1)
        mode, _, commit = header.split()
        if mode == b'160000':
            links[os.fsdecode(path)] = commit.decode()
    return links


def snapshot(repo, base):
    links = gitlinks(repo, base)
    for path, commit in links.items():
        nested = repo / path
        if not (nested / '.git').exists():
            continue
        head = c.git(nested, 'rev-parse', 'HEAD').stdout.decode().strip()
        dirty = c.git(nested, 'status', '--porcelain', '--untracked-files=all').stdout
        if head != commit or dirty:
            raise ValueError(f'{nested}: nested dependency changed. Preserve it and update its explicit dependency pin/patch; it cannot be hidden in the parent patch.')
    with index(repo, base) as env:
        paths = ['.', *[':(exclude)' + path for path in links]]
        c.git(repo, 'add', '--all', '--', *paths, env=env)
        return c.git(repo, 'write-tree', env=env).stdout.decode().strip()


def diff(repo, before, after):
    return c.git(repo, 'diff', '--no-ext-diff', '--binary', '--full-index', '--find-renames',
                 '--src-prefix=a/', '--dst-prefix=b/', before, after, '--').stdout


class Project:
    def __init__(self, name, validate=True):
        self.name = name
        self.record = c.config()['projects'][name]
        self.path = c.ROOT / self.record['path']
        records = c.git(c.ROOT, 'ls-files', '--stage', '-z', '--', self.record['path']).stdout.split(b'\0')
        records = [record for record in records if record]
        if len(records) != 1:
            raise ValueError(f'{name}: missing registered Gitlink; use a Git checkout and git submodule update --init.')
        header, path = records[0].split(b'\t', 1)
        mode, commit, stage = header.split()
        if mode != b'160000' or stage != b'0':
            raise ValueError(f'{name}: source path must be an unconflicted submodule Gitlink.')
        self.base = commit.decode()
        if not (self.path / '.git').exists():
            raise ValueError(f'{name}: uninitialized submodule; run git submodule update --init.')
        head = c.git(self.path, 'rev-parse', 'HEAD').stdout.decode().strip()
        if head != self.base:
            raise ValueError(f'{name}: HEAD {head} differs from pinned Gitlink {self.base}; no checkout/reset was attempted.')
        self.base_tree = c.git(self.path, 'rev-parse', self.base + '^{tree}').stdout.decode().strip()
        self.patch_paths = [c.ROOT / item['path'] for item in self.record['patches']]
        if validate:
            for item, path in zip(self.record['patches'], self.patch_paths):
                if c.sha(path) != item['sha256']:
                    raise ValueError(f'{path}: checksum mismatch. Export intentional source edits; do not bypass validation.')

    def expected(self):
        with index(self.path, self.base) as env:
            for patch in self.patch_paths:
                if patch.stat().st_size:
                    c.git(self.path, 'apply', '--cached', '--check', patch, env=env)
                    c.git(self.path, 'apply', '--cached', patch, env=env)
            return c.git(self.path, 'write-tree', env=env).stdout.decode().strip()

    def identity(self, require_saved=False):
        tree = self.expected()
        result = {'upstream_commit': self.base, 'patches': self.record['patches'], 'tree': tree}
        result['fingerprint'] = c.fingerprint(result)
        if require_saved and snapshot(self.path, self.base) != tree:
            raise ValueError(f'{self.name}: unexported source edits. Run python3 scripts/patches.py export {self.name} before building release packages.')
        return result

    @property
    def state_path(self):
        return c.ROOT / 'build/state/patches' / (self.name + '.json')

    def state(self):
        return json.loads(self.state_path.read_text()) if self.state_path.exists() else {}

    def remember(self):
        c.atomic_json(self.state_path, self.identity())


def identity(name, require_saved=False):
    return Project(name).identity(require_saved)


def apply(names):
    actions = []
    for name in names:
        project = Project(name)
        expected = project.expected()
        actual = snapshot(project.path, project.base)
        state = project.state()
        if actual == expected:
            action = 'saved'
        elif actual == project.base_tree:
            action = 'apply'
        elif state.get('fingerprint') == project.identity()['fingerprint']:
            action = 'preserve'
        elif state.get('upstream_commit') == project.base and state.get('tree') == actual:
            action = 'refresh'
        else:
            raise ValueError(f'{name}: source content conflicts with the saved patch state. Local files are untouched; inspect/export your edits.')
        actions.append((project, action, actual, expected))
    # Every project has been preflighted before changing any source files.
    for project, action, actual, expected in actions:
        if action in ['apply', 'refresh']:
            data = diff(project.path, actual, expected)
            with tempfile.NamedTemporaryFile(suffix='.patch') as patch:
                patch.write(data)
                patch.flush()
                c.git(project.path, 'apply', '--check', patch.name)
                c.git(project.path, 'apply', patch.name)
            if snapshot(project.path, project.base) != expected:
                raise ValueError(f'{project.name}: applied content failed verification.')
        project.remember()
        print(f'{project.name}: ' + ('local edits preserved (export before build)' if action == 'preserve' else 'patches applied and saved'))


def export(names):
    lock = c.config()
    pending = []
    for name in names:
        project = Project(name, validate=False)
        if len(project.patch_paths) != 1:
            raise ValueError('Export currently supports one cumulative patch per project.')
        tree = snapshot(project.path, project.base)
        if tree == project.base_tree:
            raise ValueError(f'{name}: no changes from upstream. Refusing to erase the compatibility patch.')
        data = diff(project.path, project.base, tree)
        with index(project.path, project.base) as env:
            with tempfile.NamedTemporaryFile(suffix='.patch') as stream:
                stream.write(data)
                stream.flush()
                c.git(project.path, 'apply', '--cached', '--check', stream.name, env=env)
                c.git(project.path, 'apply', '--cached', stream.name, env=env)
            reconstructed = c.git(project.path, 'write-tree', env=env).stdout.decode().strip()
            if reconstructed != tree:
                raise ValueError(f'{name}: patch export did not reproduce the working tree.')
        pending.append((project, data))
    for project, data in pending:
        c.atomic_bytes(project.patch_paths[0], data)
        lock['projects'][project.name]['patches'][0]['sha256'] = hashlib.sha256(data).hexdigest()
    c.atomic_json(c.ROOT / 'sources.lock.json', lock)
    for project, data in pending:
        Project(project.name).remember()
        print(f'{project.name}: exported {len(data)} bytes to {project.record["patches"][0]["path"]}')


def unapply(names):
    pending = []
    for name in names:
        project = Project(name)
        actual = snapshot(project.path, project.base)
        expected = project.expected()
        if actual not in [expected, project.base_tree]:
            raise ValueError(f'{name}: unexported edits exist; refusing to remove them.')
        pending.append((project, actual))
    for project, actual in pending:
        if actual != project.base_tree:
            with tempfile.NamedTemporaryFile(suffix='.patch') as patch:
                patch.write(diff(project.path, actual, project.base_tree))
                patch.flush()
                c.git(project.path, 'apply', '--check', patch.name)
                c.git(project.path, 'apply', patch.name)
        print(f'{project.name}: upstream files restored; saved patch retained')


def status(names):
    for name in names:
        project = Project(name)
        actual = snapshot(project.path, project.base)
        expected = project.expected()
        state = 'saved' if actual == expected else 'unapplied' if actual == project.base_tree else 'modified / unexported'
        print(f'{name:14} {project.base[:10]}  {state}')


def check(names):
    for name in names:
        project = Project(name)
        expected = project.expected()
        if snapshot(project.path, project.base) != expected:
            raise ValueError(f'{name}: working files differ from exported patches.')
        with tempfile.TemporaryDirectory(prefix='cp2077-patch-check-') as directory:
            repo = pathlib.Path(directory) / 'source'
            c.run(['git', 'clone', '--no-hardlinks', '--no-checkout', project.path, repo])
            c.git(repo, 'checkout', '--detach', project.base)
            for patch in project.patch_paths:
                if patch.stat().st_size:
                    c.git(repo, 'apply', '--check', patch)
                    c.git(repo, 'apply', patch)
            if snapshot(repo, project.base) != expected:
                raise ValueError(f'{name}: independent reconstruction differs.')
        print(f'{name}: independent source reconstruction passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['apply', 'export', 'status', 'check', 'unapply'])
    parser.add_argument('project', nargs='?', default='all')
    args = parser.parse_args()
    globals()[args.command](c.select(args.project))


if __name__ == '__main__':
    c.entry(main)
