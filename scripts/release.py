#!/usr/bin/env python3
"""Prepare flat GitHub release assets; verification uses patch-helper Git objects."""
import argparse
import copy
import json
import pathlib
import re
import shutil
import tempfile

import common as c
import verification


def prepare(tag):
    if not re.fullmatch(r'v[0-9]+\.[0-9]+\.[0-9]+(?:-rc(?:\.[0-9]+)?)?', tag):
        raise ValueError('Use vX.Y.Z, vX.Y.Z-rc or vX.Y.Z-rc.N.')
    source = c.ROOT / 'dist/release'
    manifest = json.loads((source / 'manifest.json').read_text())
    expected = set(c.NAMES) - {'red4ext-sdk'}
    if manifest.get('config') != 'Release' or set(manifest.get('projects', {})) != expected:
        raise ValueError('A GitHub release requires all six Release packages.')
    for project, record in manifest['projects'].items():
        kinds = {'runtime', 'symbols', 'standard'} if project in c.ROOT_LAYOUT else {'runtime', 'symbols'}
        if set(record['files']) != kinds:
            raise ValueError(f'{project}: incomplete release archive set.')
    verification.distribution(source)
    manifest = copy.deepcopy(manifest)
    manifest['essentials_release'] = tag
    destination = c.ROOT / 'build/github-release'
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.github-release-', dir=destination.parent) as temporary:
        stage = pathlib.Path(temporary) / 'new'
        stage.mkdir()
        assets = []
        sums = []
        seen = set()
        for record in manifest['projects'].values():
            for item in record['files'].values():
                path = source / item['file']
                if not path.resolve().is_relative_to(source.resolve()):
                    raise ValueError('Release archive escapes its distribution directory.')
                name = path.name
                if name in seen:
                    raise ValueError(f'Duplicate release asset name: {name}')
                seen.add(name)
                if c.sha(path) != item['sha256']:
                    raise ValueError(f'{name}: package changed during release preparation.')
                shutil.copy2(path, stage / name)
                line = f'{item["sha256"]}  {name}\n'
                c.atomic_bytes(stage / (name + '.sha256'), line.encode())
                sums.append(line)
                assets += [name, name + '.sha256']
                item['file'] = name
        c.atomic_json(stage / 'manifest.json', manifest)
        c.atomic_bytes(stage / 'SHA256SUMS', ''.join(sums).encode())
        assets += ['manifest.json', 'SHA256SUMS']
        # Only these files are uploaded; notes and the list itself are local helpers.
        c.atomic_bytes(stage / 'assets.txt', ''.join(f'build/github-release/{name}\n' for name in assets).encode())
        versions = '\n'.join(f'| {record["project"]} | {record["version"]} |' for record in manifest['projects'].values())
        notes = f'''Unofficial, vibe-coded compatibility builds for **Cyberpunk 2077 2.5**
(executable **3.0.88.46235**). Original mods belong to their authors.

| Mod | Original version |
| --- | --- |
{versions}

Install the six runtime ZIPs with MO2. For RED4ext and Cyber Engine Tweaks, choose either
`*-mo2-rootbuilder.zip` (requires Root Builder) or `*-game-root.zip` (manual
game-root installation), never both variants of the same mod. `*-symbols.zip` files are optional PDBs.

Disable older versions of these six mods. On Proton, preserve the native
`winmm` override. All DLLs are compiled from pinned official Git submodules
with this repository's saved patches and shared SDK. The manifest retains
source revisions and patch checksums; SHA256SUMS covers all fourteen archives.
ArchiveXL's unchanged packed resource is from the pinned official release.
CET's two TweakDB metadata files are from its checksum-pinned official release;
its bundled ASI and loader DLL are never reused. CET loads as a RED4ext plugin,
while Lua mods stay at `bin/x64/plugins/cyber_engine_tweaks/mods/`.
Remove the older CET ASI, version.dll and global.ini when replacing an old CET
installation, unless another mod independently requires that ASI loader.

Release builds and static package checks pass. The previous local essentials
set started the game; CET, Input Loader and these CI-built binaries have not been
playtested. Gameplay, save/load and save compatibility remain untested.
Input Loader may warn that vanilla 2.5 input XML hashes are unknown.
ArchiveXL journal indexing and resource hot reload are disabled; cooked
instanced-mesh/collision-node element edits remain limited.
'''
        c.atomic_bytes(stage / 'notes.md', notes.encode())
        previous = pathlib.Path(temporary) / 'previous'
        if destination.exists():
            destination.rename(previous)
        try:
            stage.rename(destination)
        except OSError:
            if previous.exists():
                previous.rename(destination)
            raise
    print(f'Prepared {len(seen)} archives and checksums in {destination}')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tag')
    prepare(parser.parse_args().tag)


if __name__ == '__main__':
    c.entry(main)
