#!/usr/bin/env python3
"""Build complete runtime and symbol archives from the validated upstream package layout."""
import hashlib
import pathlib
import re
import struct
import zipfile
import common as c
import patches

INPUT_LOADER_NEUTRAL_INI = (
    b'; Input Loader essentials: neutral until a complete cache pair is ready\n'
    b'[Player/Input]\n'
)

def original_version(project, source, build):
    if project == 'input-loader':
        return c.config()['projects'][project]['version']
    if project == 'red4ext':
        text = (build / 'src/loader/Resource.rc').read_text()
        pattern = r'VALUE "ProductVersion", "([0-9]+\.[0-9]+\.[0-9]+)'
    else:
        text = (source / 'xmake.lua').read_text()
        pattern = r'set_version\("([0-9]+\.[0-9]+\.[0-9]+)"'
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f'{project}: missing original numeric mod version.')
    return match[1]


def archive_name(project, version, config, kind):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError(f'{project}: invalid original mod version {version!r}.')
    if config not in ['Release', 'Debug']:
        raise ValueError(f'Unsupported package configuration: {config}')
    suffixes = {'runtime': '-mo2-rootbuilder' if project == 'red4ext' else '',
                'symbols': '-symbols'}
    if project == 'red4ext':
        suffixes['standard'] = '-game-root'
    if kind not in suffixes:
        raise ValueError(f'{project}: unsupported archive kind {kind}.')
    mode = '-debug' if config == 'Debug' else ''
    return f'{c.NAMES[project]}-{version}-game2.5{mode}{suffixes[kind]}.zip'


def input_loader_payload(source, build):
    prefix = 'red4ext/plugins/input_loader/'
    paths = {
        prefix + 'input_loader.dll': build / 'bin/input_loader.dll',
        prefix + 'readme.md': source / 'readme.md',
        prefix + 'license.md': source / 'license.md',
        prefix + 'licenses/pugixml.txt': source / 'deps/pugixml/LICENSE.md',
        prefix + 'licenses/spdlog.txt': c.ROOT / c.config()['dependencies']['spdlog']['path'] / 'LICENSE',
        'engine/config/platform/pc/input_loader.ini': source / 'input_loader.ini',
    }
    payload = {name: path.read_bytes() for name, path in paths.items()}
    if payload['engine/config/platform/pc/input_loader.ini'] != INPUT_LOADER_NEUTRAL_INI:
        raise ValueError('Input Loader package must ship the neutral selector, without generated cache paths')
    return payload


def create(project, config):
    root = c.ROOT
    name = c.NAMES[project]
    source = root / c.config()['projects'][project]['path']
    sdk = patches.identity('red4ext-sdk', require_saved=True)
    identity = patches.identity(project, require_saved=True)
    build = root / 'build' / ('red4ext' if project == 'red4ext' else 'plugins') / config.lower()
    out = root / 'build/packages' / config.lower() / project
    out.mkdir(parents=True, exist_ok=True)
    version = original_version(project, source, build)
    if project == 'red4ext':
        prefix = ''
        install = build / 'install'
        runtime = {p: (install / p).read_bytes() for p in ['bin/x64/winmm.dll', 'red4ext/RED4ext.dll', 'red4ext/LICENSE.txt', 'red4ext/THIRD_PARTY_LICENSES.txt']}
        symbols = {p: (install / p).read_bytes() for p in ['bin/x64/winmm.pdb', 'red4ext/RED4ext.pdb']}
    elif project == 'input-loader':
        runtime = input_loader_payload(source, build)
        symbols = {'red4ext/plugins/input_loader/input_loader.pdb':
                   (build / 'bin/input_loader.pdb').read_bytes()}
    else:
        prefix = f'red4ext/plugins/{name}/'
        runtime = {}
        def include(path, archive_name):
            if not path.is_file():
                raise SystemExit(f'Missing package input: {path}')
            runtime[archive_name] = path.read_bytes()
        dll = build / 'bin' / f'{name}.dll'
        include(dll, prefix + dll.name)
        pe = runtime[prefix + dll.name]
        offset = struct.unpack_from('<I', pe, 0x3c)[0]
        assert pe[:2] == b'MZ' and pe[offset:offset+4] == b'PE\0\0' and struct.unpack_from('<H', pe, offset+4)[0] == 0x8664
        for license_file in ['LICENSE', 'THIRD_PARTY_LICENSES']:
            include(source / license_file, prefix + license_file)
        # Group modules and deduplicate imports exactly as the upstream PowerShell composer.
        bundles = {}
        for path in sorted((source / 'scripts').rglob('*.reds')):
            module = None
            imports = []
            body = []
            for line in path.read_text(encoding='utf-8-sig').splitlines():
                if match := re.match(r'^module\s+(.+)', line):
                    module = match[1].strip()
                elif match := re.match(r'^import\s+(.+)', line):
                    imports.append(match[1].strip())
                elif not re.match(r'^\s*(//|module\s|import\s|$)', line):
                    body.append(line)
            key = module or f'{name}.Global'
            bundle = bundles.setdefault(key, {'module': module, 'imports': set(), 'body': []})
            bundle['imports'].update(imports)
            bundle['body'].append('\n'.join(body).strip())
        for scope, bundle in sorted(bundles.items()):
            parts = [f'// {name} {version} (game 2.5 essentials)']
            if bundle['module']:
                parts.append('module ' + bundle['module'])
            parts += ['import ' + value for value in sorted(bundle['imports'])]
            parts += ['\n' + body for body in bundle['body'] if body]
            runtime[prefix + f'Scripts/{scope}.reds'] = ('\n'.join(parts) + '\n').encode()
        if name in ['Codeware', 'TweakXL']:
            for path in sorted((source / 'data').iterdir()):
                if path.is_file():
                    include(path, prefix + 'Data/' + path.name)
        if name == 'ArchiveXL':
            # Packed WolvenKit resource asset is unchanged by the 2.5 source port.
            assets = c.config()['archivexl_assets']
            packed = root / assets['path']
            assert hashlib.sha256(packed.read_bytes()).hexdigest() == assets['sha256']
            with zipfile.ZipFile(packed) as z:
                archive_path = prefix + 'Bundle/ArchiveXL.archive'
                runtime[archive_path] = z.read(archive_path)
            for path in sorted((source / 'bundle/source/resources').iterdir()):
                if path.is_file():
                    include(path, prefix + 'Bundle/' + path.name)
            include(source / 'config/Config.yaml', prefix + name + '.yaml')
            for path in sorted((source / 'support/hints').iterdir()):
                if path.is_file():
                    include(path, 'r6/config/redsUserHints/' + path.name)
        pdb = build / 'bin' / f'{name}.pdb'
        symbols = {prefix + pdb.name: pdb.read_bytes()}
    groups = [('runtime', runtime, 'Root/' if project == 'red4ext' else ''),
              ('symbols', symbols, '')]
    if project == 'red4ext':
        groups += [('standard', runtime, '')]
    record = {'project': name, 'version': version, 'config': config,
              'source': identity, 'sdk': sdk, 'files': {}}
    for kind, contents, root_prefix in groups:
        dest = out / archive_name(project, version, config, kind)
        with zipfile.ZipFile(dest, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path, data in sorted(contents.items()):
                archive.writestr(root_prefix + path, data)
        with zipfile.ZipFile(dest) as archive:
            if archive.testzip() is not None:
                raise ValueError(f'ZIP CRC verification failed: {dest}')
        digest = c.sha(dest)
        c.atomic_bytes(dest.with_suffix('.zip.sha256'), f'{digest}  {dest.name}\n'.encode())
        record['files'][kind] = {'file': str(dest.relative_to(root)), 'sha256': digest}
        print(dest, flush=True)
    c.atomic_json(out / 'package.json', record)
    return record
