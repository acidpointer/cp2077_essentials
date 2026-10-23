"""Bootstrap Microsoft's x64 CRT/SDK with checksum-pinned native Linux xwin."""
import os
import pathlib
import platform
import shutil
import urllib.request

import common as c


def ensure():
    for tool in ['cmake', 'clang-cl', 'lld-link', 'llvm-lib', 'llvm-mt', 'llvm-rc', 'llvm-ml', 'llvm-readobj', 'llvm-pdbutil']:
        if not shutil.which(tool):
            raise ValueError(f'Missing tool: {tool}; see README.md')
    if platform.system() != 'Linux':
        raise ValueError('The build scripts require Linux.')
    root = pathlib.Path(os.environ.get('XWIN_ROOT', c.ROOT / 'build/toolchains/xwin')).resolve()
    if (root / 'crt/include/vcruntime.h').is_file():
        return root
    if platform.machine() != 'x86_64':
        raise ValueError('Provide a prepared XWIN_ROOT on non-x86_64 hosts.')
    tools = c.ROOT / 'build/toolchains/bin'
    tools.mkdir(parents=True, exist_ok=True)
    binary = tools / 'xwin'
    if not binary.exists():
        archive = tools / 'xwin-0.10.0.tar.gz'
        url = 'https://github.com/Jake-Shadle/xwin/releases/download/0.10.0/xwin-0.10.0-x86_64-unknown-linux-musl.tar.gz'
        with urllib.request.urlopen(url, timeout=120) as response:
            archive.write_bytes(response.read())
        if c.sha(archive) != 'd870eb4b2f390878af6da1ccd3cf321d22fcb72720984853b4be732ae597fc88':
            raise ValueError('xwin download checksum mismatch')
        c.run(['tar', '-xzf', archive, '-C', tools, '--strip-components=1'])
    print('Downloading Microsoft CRT/SDK via xwin --accept-license.', flush=True)
    result = c.run([binary, '--accept-license', '--http-retry', '3', '--cache-dir',
                   c.ROOT / 'build/toolchains/xwin-cache', '--arch', 'x86_64',
                   'splat', '--include-debug-libs', '--output', root])
    print(result.stdout.decode(errors='replace'), end='')
    return root
