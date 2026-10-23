#!/usr/bin/env python3
"""Initialize recorded submodules, apply patches, and prepare pinned dependencies."""
import argparse
import pathlib
import urllib.request

import common as c
import patches


def sources():
    paths = [item['path'] for item in c.config()['projects'].values()]
    # No --remote, --force, or blanket recursive update of unused plugin SDKs.
    missing = []
    for name, item in c.config()['projects'].items():
        if (c.ROOT / item['path'] / '.git').exists():
            patches.Project(name)  # Refuse a changed HEAD before any checkout.
        else:
            missing.append(item['path'])
    if missing:
        c.run(['git', 'submodule', 'update', '--init', '--', *missing])
    patches.apply(c.select('all'))


def dependencies():
    lock = c.config()
    for name, item in lock['dependencies'].items():
        path = c.ROOT / item['path']
        if not (path / '.git').exists():
            path.parent.mkdir(parents=True, exist_ok=True)
            c.run(['git', 'clone', item['url'], path])
            if c.git(path, 'cat-file', '-e', item['commit'] + '^{commit}', check=False).returncode:
                c.git(path, 'fetch', 'origin', item['commit'])
            c.git(path, 'checkout', '--detach', item['commit'])
        if c.git(path, 'rev-parse', 'HEAD').stdout.decode().strip() != item['commit']:
            raise ValueError(f'{name}: dependency differs from lock; preserve edits instead of resetting it.')
        if c.git(path, 'status', '--porcelain').stdout:
            raise ValueError(f'{name}: dependency has local edits; an explicit dependency patch is required.')
    for name in ['archive-xl', 'codeware', 'tweak-xl']:
        path = c.ROOT / lock['projects'][name]['path']
        paths = ['vendor/nameof', 'vendor/semver', 'vendor/wil']
        paths += ['vendor/clip'] if name == 'codeware' else ['vendor/pegtl'] if name == 'tweak-xl' else []
        links = patches.gitlinks(path, c.git(path, 'rev-parse', 'HEAD').stdout.decode().strip())
        missing = []
        for nested_path in paths:
            nested = path / nested_path
            if (nested / '.git').exists():
                head = c.git(nested, 'rev-parse', 'HEAD').stdout.decode().strip()
                if head != links[nested_path] or c.git(nested, 'status', '--porcelain').stdout:
                    raise ValueError(f'{nested}: local dependency changes exist; no checkout/reset was attempted.')
            else:
                missing.append(nested_path)
        if missing:
            c.git(path, 'submodule', 'update', '--init', '--recursive', '--', *missing)
    source = c.ROOT / lock['projects']['input-loader']['path']
    nested_path = 'deps/pugixml'
    nested = source / nested_path
    base = c.git(source, 'rev-parse', 'HEAD').stdout.decode().strip()
    pin = patches.gitlinks(source, base)[nested_path]
    if not (nested / '.git').exists():
        c.git(source, 'submodule', 'update', '--init', '--', nested_path)
    if c.git(nested, 'rev-parse', 'HEAD').stdout.decode().strip() != pin or c.git(nested, 'status', '--porcelain').stdout:
        raise ValueError(f'{nested}: local dependency changes exist; no checkout/reset was attempted.')
    asset = lock['archivexl_assets']
    path = c.ROOT / asset['path']
    if not path.exists():
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix('.download')
        with urllib.request.urlopen(asset['url'], timeout=120) as response:
            temporary.write_bytes(response.read())
        if c.sha(temporary) != asset['sha256']:
            raise ValueError('ArchiveXL resource bundle checksum mismatch')
        temporary.replace(path)
    if c.sha(path) != asset['sha256']:
        raise ValueError('ArchiveXL resource bundle checksum mismatch')
    print('Pinned dependencies ready.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources-only', action='store_true')
    args = parser.parse_args()
    sources()
    if not args.sources_only:
        dependencies()


if __name__ == '__main__':
    c.entry(main)
