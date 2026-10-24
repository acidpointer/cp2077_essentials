#!/usr/bin/env python3
"""Build isolated native regressions; optionally run them and compile packaged scripts."""
import argparse
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import zipfile

import common as c
import package


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=c.ROOT / 'build/mod-settings-checks')
    parser.add_argument('--runner', help='Optional Windows executable runner, such as wine')
    parser.add_argument('--scc-dir', type=Path, help='Directory containing scc.exe and scc_lib.dll')
    parser.add_argument('--vanilla-cache', type=Path, help='Vanilla 2.5 final.redscripts to copy for an isolated compile')
    parser.add_argument('--runtime-zip', type=Path, help='Mod Settings runtime archive (default: current dist/release package)')
    args = parser.parse_args()
    if bool(args.scc_dir) != bool(args.vanilla_cache) or (args.scc_dir and not args.runner):
        parser.error('--scc-dir and --vanilla-cache require each other and --runner')
    output = args.output.resolve()
    if not output.is_relative_to(c.ROOT / 'build'):
        parser.error('--output must stay under build/')
    output.mkdir(parents=True, exist_ok=True)
    build = output / 'windows'
    configure = ['cmake', '-S', c.ROOT / 'tests/native/mod-settings', '-B', build,
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(c.ROOT / 'cmake/toolchains/clang-cl-linux.cmake'),
                 '-DXWIN_ROOT=' + str(c.ROOT / 'build/toolchains/xwin'), '-DCMAKE_BUILD_TYPE=Release']
    if not (build / 'CMakeCache.txt').exists():
        configure += ['-G', 'Ninja' if shutil.which('ninja') else 'Unix Makefiles']
    with (output / 'build.log').open('w') as log:
        for command in [configure, ['cmake', '--build', build, '--parallel', '2']]:
            subprocess.run([str(value) for value in command], stdout=log, stderr=subprocess.STDOUT, check=True)
    if args.runner:
        runner = shlex.split(args.runner)
        wine = Path(runner[0]).name.startswith('wine')
        env = dict(os.environ)
        if wine:
            env.update(WINEPREFIX=str(output / 'wine-prefix'), WINEDEBUG='-all',
                       WINEDLLOVERRIDES='winemenubuilder.exe=d')
        with (output / 'runtime.log').open('w') as log:
            subprocess.run([*runner, str(build / 'mod-settings-runtime.exe')], env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True, timeout=60)
        print('Native callback and hook regressions passed.')
        if args.scc_dir:
            version = c.config()['projects']['mod-settings']['version']
            archive = (args.runtime_zip or c.ROOT / 'dist/release' /
                       package.archive_name('mod-settings', version, 'Release', 'runtime')).resolve()
            evidence = {'runtime_zip_sha256': c.sha(archive), 'vanilla_cache_sha256': c.sha(args.vanilla_cache),
                        'scc_sha256': c.sha(args.scc_dir / 'scc.exe'),
                        'scc_lib_sha256': c.sha(args.scc_dir / 'scc_lib.dll')}
            # The compiler receives only a private game-shaped tree. It may create
            # logs/backups there; the real game and MO2 paths are never outputs.
            with tempfile.TemporaryDirectory(prefix='script-input-', dir=output) as temporary:
                root = Path(temporary)
                scripts = root / 'r6/scripts/mod_settings'
                scripts.mkdir(parents=True)
                cache = root / 'r6/cache/final.redscripts'
                cache.parent.mkdir(parents=True)
                shutil.copyfile(args.vanilla_cache, cache)
                for name in ['scc.exe', 'scc_lib.dll']:
                    shutil.copyfile(args.scc_dir / name, root / name)
                with zipfile.ZipFile(archive) as packed:
                    for name in ['packed.reds', 'module.reds']:
                        (scripts / name).write_bytes(packed.read('red4ext/plugins/mod_settings/' + name))
                def platform_path(path):
                    return 'Z:' + str(path).replace('/', '\\') if wine else str(path)
                compiled = root / 'compiled.redscripts'
                command = [*runner, str(root / 'scc.exe'), '-compile', platform_path(scripts.parent),
                           '-outputCacheFile', platform_path(compiled)]
                with (output / 'redscript.log').open('w') as log:
                    subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   check=True, timeout=60)
                if not compiled.is_file():
                    raise RuntimeError('The compiler did not produce an isolated output cache.')
                shutil.copyfile(compiled, output / 'compiled.redscripts')
            evidence['compiled_cache_sha256'] = c.sha(output / 'compiled.redscripts')
            c.atomic_json(output / 'redscript-evidence.json', evidence)
            print('Packaged scripts compiled against the supplied vanilla cache.')
    print('These are isolated checks; no game startup or playtest was performed.')


if __name__ == '__main__':
    c.entry(main)
