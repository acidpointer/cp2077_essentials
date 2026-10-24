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
    if project in ['input-loader', 'cyber-engine-tweaks', 'mod-settings']:
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
    suffixes = {'runtime': '-mo2-rootbuilder' if project in c.ROOT_LAYOUT else '',
                'symbols': '-symbols'}
    if project in c.ROOT_LAYOUT:
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


def cet_dependencies():
    lock = c.config()
    names = [name for name in lock['dependencies'] if name.startswith('cet-')]
    names += ['tiltedcore', 'mimalloc', 'minhook', 'hopscotch-map', 'spdlog']
    return {'sources': {name: lock['dependencies'][name] for name in names},
            'sqlite': lock['sqlite_asset'], 'assets': lock['cet_assets']}


def mod_settings_dependencies():
    import mod_settings
    item = c.config()['mod_settings_redlib']
    return {'redlib': item, 'redlib_overlay': mod_settings.check(), 'archivexl_api_sha256': c.sha(
        c.ROOT / 'sources/archive-xl/support/red4ext/ArchiveXL.hpp')}


def mod_settings_payload(source, build):
    prefix = 'red4ext/plugins/mod_settings/'
    runtime = {prefix + 'mod_settings.dll': (build / 'bin/mod_settings.dll').read_bytes()}
    for name in ['readme.md', 'license.md']:
        runtime[prefix + name] = (source / name).read_bytes()
    runtime[prefix + 'ModSettings.archive'] = (
        source / 'src/wolvenkit/packed/archive/pc/mod/ModSettings.archive').read_bytes()
    runtime[prefix + 'ModSettings.archive.xl'] = (source / 'src/archiveXL/ModSettings.archive.xl').read_bytes()
    # Match upstream's composer: all global script files, then a separate
    # versioned module. Keep comments, annotations and per-file boundaries.
    parts = []
    for path in sorted((source / 'src/redscript').rglob('*.reds')):
        parts += ['// ' + path.relative_to(source / 'src/redscript').as_posix(),
                  path.read_text(encoding='utf-8-sig')]
    if not parts:
        raise ValueError('Mod Settings: missing redscript sources.')
    runtime[prefix + 'packed.reds'] = ('\n\n'.join(parts) + '\n').encode()
    module = (source / 'src/redscript/Module.reds.in').read_text()
    version = c.config()['projects']['mod-settings']['version']
    for name, value in zip(['MAJOR', 'MINOR', 'PATCH'], version.split('.')):
        module = module.replace('@PROJECT_VERSION_' + name + '@', value)
    runtime[prefix + 'module.reds'] = module.encode()
    notices = {'RedLib.txt': source / 'deps/red_lib/LICENSE',
               'nameof.txt': source / 'deps/red_lib/vendor/nameof/LICENSE',
               'RED4ext.SDK.txt': c.ROOT / 'sources/red4ext-sdk/LICENSE.md',
               'ArchiveXL.txt': c.ROOT / 'sources/archive-xl/LICENSE'}
    for name, path in notices.items():
        runtime[prefix + 'licenses/' + name] = path.read_bytes()
    return runtime


def cet_payload(source, build):
    """Keep upstream Lua mod paths, with the native plugin loaded by RED4ext."""
    data_prefix = 'bin/x64/plugins/cyber_engine_tweaks/'
    plugin_prefix = 'red4ext/plugins/CyberEngineTweaks/'
    runtime = {plugin_prefix + 'CyberEngineTweaks.dll': (build / 'bin/CyberEngineTweaks.dll').read_bytes(),
               plugin_prefix + 'LICENSE': (source / 'LICENSE').read_bytes(),
               data_prefix + 'ThirdParty_LICENSES': (source / 'ThirdParty_LICENSES').read_bytes()}
    for directory in ['scripts', 'fonts']:
        for path in sorted((source / directory).rglob('*')):
            if path.is_file():
                runtime[data_prefix + path.relative_to(source).as_posix()] = path.read_bytes()
    asset = c.config()['cet_assets']
    packed = c.ROOT / asset['path']
    if c.sha(packed) != asset['sha256']:
        raise ValueError('CET resource bundle checksum mismatch')
    with zipfile.ZipFile(packed) as archive:
        for name in ['tweakdb/usedhashes.kark', 'tweakdb/tweakdbstr.kark']:
            runtime[data_prefix + name] = archive.read(data_prefix + name)
    # Upstream's third-party notices cover fonts and embedded bindings. Include
    # the exact dependency notices too, notably FreeType's additional license.
    notices = {'cet-json': ['LICENSE.MIT'], 'cet-mem': ['LICENSE.txt'],
               'cet-sol2': ['LICENSE.txt'], 'cet-luajit': ['COPYRIGHT'],
               'cet-imgui': ['LICENSE.txt'], 'cet-xbyak': ['COPYRIGHT'],
               'cet-stb': ['LICENSE'], 'cet-freetype': ['docs/FTL.TXT', 'docs/GPLv2.TXT'],
               'tiltedcore': ['LICENSE'], 'mimalloc': ['LICENSE'], 'minhook': ['LICENSE.txt'],
               'hopscotch-map': ['LICENSE'], 'spdlog': ['LICENSE']}
    for name, paths in notices.items():
        dependency = c.ROOT / c.config()['dependencies'][name]['path']
        for path in paths:
            runtime[plugin_prefix + 'licenses/' + name + '/' + pathlib.PurePosixPath(path).name] = (dependency / path).read_bytes()
    return runtime


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
    elif project == 'cyber-engine-tweaks':
        runtime = cet_payload(source, build)
        symbols = {'red4ext/plugins/CyberEngineTweaks/CyberEngineTweaks.pdb':
                   (build / 'bin/CyberEngineTweaks.pdb').read_bytes()}
    elif project == 'mod-settings':
        runtime = mod_settings_payload(source, build)
        symbols = {'red4ext/plugins/mod_settings/mod_settings.pdb':
                   (build / 'bin/mod_settings.pdb').read_bytes()}
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
    groups = [('runtime', runtime, 'Root/' if project in c.ROOT_LAYOUT else ''),
              ('symbols', symbols, '')]
    if project in c.ROOT_LAYOUT:
        groups += [('standard', runtime, '')]
    record = {'project': name, 'version': version, 'config': config,
              'source': identity, 'sdk': sdk, 'files': {}}
    if project == 'cyber-engine-tweaks':
        record['dependencies'] = cet_dependencies()
    elif project == 'mod-settings':
        record['dependencies'] = mod_settings_dependencies()
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
