"""Verify release provenance, native binaries, symbols, payloads, and game hashes."""
import collections
import json
import pathlib
import re
import struct
import zipfile

import common as c
import patches
import package


def exports(dll):
    text = c.run(['llvm-readobj', '--coff-exports', dll]).stdout.decode()
    return set(re.findall(r'Export \{\s+Ordinal: \d+\s+Name: (\S+)', text))


def binary(dll, pdb):
    pe = dll.read_bytes()
    offset = struct.unpack_from('<I', pe, 0x3c)[0]
    if pe[:2] != b'MZ' or pe[offset:offset+4] != b'PE\0\0' or struct.unpack_from('<H', pe, offset+4)[0] != 0x8664:
        raise ValueError(f'{dll}: not a Windows AMD64 DLL')
    imports = c.run(['llvm-readobj', '--coff-imports', dll]).stdout.decode().lower()
    if any(item in imports for item in ['libgcc', 'libstdc++', 'libwinpthread']):
        raise ValueError(f'{dll}: incompatible MinGW runtime import')
    debug = c.run(['llvm-readobj', '--coff-debug-directory', dll]).stdout.decode()
    summary = c.run(['llvm-pdbutil', 'dump', '-summary', pdb]).stdout.decode()
    if re.search(r'PDBGUID: (\{[^}]+\})', debug)[1] != re.search(r'GUID: (\{[^}]+\})', summary)[1]:
        raise ValueError(f'{dll}: PDB identifier mismatch')


def distribution(folder):
    folder = pathlib.Path(folder)
    manifest = json.loads((folder / 'manifest.json').read_text())
    if manifest.get('schema') != 2:
        raise ValueError('Old distribution schema; rebuild with the submodule workflow.')
    sdk = patches.identity('red4ext-sdk')
    if manifest['sdk'] != sdk:
        raise ValueError('Distribution SDK provenance differs from the current SDK Gitlink/patch.')
    for project, record in manifest['projects'].items():
        if record['source'] != patches.identity(project) or record['sdk'] != sdk:
            raise ValueError(f'{project}: stale artifact provenance; rebuild it.')
        config = manifest['config'].lower()
        build = c.ROOT / 'build' / ('red4ext' if project == 'red4ext' else 'plugins') / config
        source = c.ROOT / c.config()['projects'][project]['path']
        version = package.original_version(project, source, build)
        if record.get('version') != version or record.get('config') != manifest['config']:
            raise ValueError(f'{project}: package version/configuration differs; repackage it.')
        for kind, item in record['files'].items():
            path = folder / item['file']
            if path.name != package.archive_name(project, version, manifest['config'], kind):
                raise ValueError(f'{path}: unexpected archive name; repackage it.')
            if c.sha(path) != item['sha256']:
                raise ValueError(f'{path}: SHA256 mismatch')
            with zipfile.ZipFile(path) as archive:
                names = archive.namelist()
                if archive.testzip() or len(names) != len(set(names)):
                    raise ValueError(f'{path}: CRC failure or duplicate path')
                if any(name.startswith('/') or '..' in pathlib.PurePosixPath(name).parts for name in names):
                    raise ValueError(f'{path}: unsafe package path')
                if kind == 'symbols':
                    if project == 'red4ext':
                        expected_symbols = {value: build / 'install' / value for value in ['bin/x64/winmm.pdb', 'red4ext/RED4ext.pdb']}
                    else:
                        basename = c.binary_name(project)
                        symbol_path = f'red4ext/plugins/{basename}/{basename}.pdb'
                        expected_symbols = {symbol_path: build / 'bin' / (basename + '.pdb')}
                    if set(names) != set(expected_symbols):
                        raise ValueError(f'{path}: invalid symbol payload')
                    for symbol, built in expected_symbols.items():
                        if archive.read(symbol) != built.read_bytes():
                            raise ValueError(f'{path}: packaged PDB differs from build output')
                elif project == 'red4ext':
                    prefix = 'Root/' if kind == 'runtime' else ''
                    expected = {'bin/x64/winmm.dll', 'red4ext/RED4ext.dll', 'red4ext/LICENSE.txt', 'red4ext/THIRD_PARTY_LICENSES.txt'}
                    if set(names) != {prefix + value for value in expected}:
                        raise ValueError(f'{path}: wrong loader package layout')
                    for value in expected:
                        if archive.read(prefix + value) != (build / 'install' / value).read_bytes():
                            raise ValueError(f'{path}: installed content differs')
                elif project == 'input-loader':
                    expected = package.input_loader_payload(source, build)
                    if set(names) != set(expected):
                        raise ValueError(f'{path}: wrong Input Loader runtime payload')
                    for name, data in expected.items():
                        if archive.read(name) != data:
                            raise ValueError(f'{path}: Input Loader payload differs: {name}')
                else:
                    prefix = f'red4ext/plugins/{c.NAMES[project]}/'
                    dll_path = prefix + c.NAMES[project] + '.dll'
                    if dll_path not in names or any(name.endswith('.pdb') for name in names):
                        raise ValueError(f'{path}: wrong runtime payload')
                    if archive.read(dll_path) != (build / 'bin' / (c.NAMES[project] + '.dll')).read_bytes():
                        raise ValueError(f'{path}: DLL differs from build output')
                    if not any(name.startswith(prefix + 'Scripts/') for name in names):
                        raise ValueError(f'{path}: missing redscript bundles')
                    if project == 'archive-xl':
                        if prefix + 'Bundle/ArchiveXL.archive' not in names or 'r6/config/redsUserHints/ArchiveXL.toml' not in names:
                            raise ValueError(f'{path}: missing ArchiveXL resources')
                    elif not any(name.startswith(prefix + 'Data/') for name in names):
                        raise ValueError(f'{path}: missing metadata')
        if project == 'red4ext':
            install = build / 'install'
            loader = install / 'bin/x64/winmm.dll'
            source = c.ROOT / c.config()['projects'][project]['path']
            expected = set(re.findall(r'^\s+(\w+)\s*=', (source / 'src/loader/Main.def').read_text(), re.M))
            if exports(loader) != expected:
                raise ValueError('winmm proxy exports differ from Main.def')
            binary(loader, loader.with_suffix('.pdb'))
            dll = install / 'red4ext/RED4ext.dll'
            if exports(dll) != {'RED4ext_ResolveAddress'}:
                raise ValueError('RED4ext address resolver export missing')
        else:
            dll = build / 'bin' / (c.binary_name(project) + '.dll')
            if not {'Main', 'Query', 'Supports'} <= exports(dll):
                raise ValueError(f'{project}: plugin exports missing')
            if project == 'input-loader' and 'Add' not in exports(dll):
                raise ValueError('Input Loader: public Add export missing')
        binary(dll, dll.with_suffix('.pdb'))
        print(f'{c.NAMES[project]}: source/SDK provenance, AMD64 DLL, exports, symbols, ZIP payload and SHA256 passed')


def addresses(path):
    counts = collections.Counter(int(value) for value in re.findall(rb'"hash":\s*"(\d+)"', pathlib.Path(path).read_bytes()))
    for name in ['codeware', 'tweak-xl', 'archive-xl']:
        source = c.ROOT / c.config()['projects'][name]['path']
        values = re.findall(r'constexpr uint32_t (\w+)\s*=\s*(\d+)\s*;', (source / 'src/Red/Addresses/Library.hpp').read_text())
        missing = {key for key, value in values if counts[int(value)] != 1}
        allowed = {'JournalTree_ProcessJournalIndex', 'ResourceLoader_OnUpdate'} if name == 'archive-xl' else set()
        if missing != allowed:
            raise ValueError(f'{name}: unexpected missing/colliding addresses: {missing}')
        print(f'{c.NAMES[name]}: {len(values)-len(missing)}/{len(values)} hashes resolve uniquely; disabled: {sorted(missing)}')
