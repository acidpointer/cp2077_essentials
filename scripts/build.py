#!/usr/bin/env python3
"""Build the patched official sources and collect verified MO2 packages."""
import argparse
import os
import pathlib
import shutil
import subprocess

import common as c
import bootstrap
import package
import patches
import toolchain
import distribution


def positive(value):
    result = int(value)
    if result < 1:
        raise argparse.ArgumentTypeError('jobs must be positive')
    return result


def logged(args, log):
    log.write('$ ' + ' '.join(map(str, args)) + '\n')
    log.flush()
    with subprocess.Popen([str(a) for a in args], cwd=c.ROOT, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, errors='replace') as process:
        for line in process.stdout:
            print(line, end='', flush=True)
            log.write(line)
        if process.wait():
            raise RuntimeError(f'Build command failed; see {log.name}')


def configure_args(build, xwin, config):
    args = ['-B', build, '-DCMAKE_TOOLCHAIN_FILE=' + str(c.ROOT / 'cmake/toolchains/clang-cl-linux.cmake'),
            '-DXWIN_ROOT=' + str(xwin), '-DCMAKE_BUILD_TYPE=' + config]
    if not (build / 'CMakeCache.txt').exists():
        args += ['-G', 'Ninja' if shutil.which('ninja') else 'Unix Makefiles']
    return args


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', choices=['Release', 'Debug'], default='Release')
    parser.add_argument('--jobs', type=positive, default=int(os.environ.get('RED4EXT_JOBS', '8')))
    parser.add_argument('--only', choices=['all', *[name for name in c.NAMES if name != 'red4ext-sdk'], *c.ALIASES], default='all')
    parser.add_argument('--dist-dir', help='Alternative distribution directory inside build/ or dist/')
    args = parser.parse_args()
    bootstrap.sources()
    bootstrap.dependencies()
    xwin = toolchain.ensure()
    selected = [name for name in c.NAMES if name != 'red4ext-sdk'] if args.only == 'all' else c.select(args.only)
    lock = c.config()
    logs = c.ROOT / 'build/logs'
    logs.mkdir(parents=True, exist_ok=True)
    for name in selected:
        before = patches.identity(name, require_saved=True)
        sdk = patches.identity('red4ext-sdk', require_saved=True)
        if name == 'mod-settings':
            import mod_settings
            mod_settings.prepare()
        dependency_reader = (package.cet_dependencies if name == 'cyber-engine-tweaks' else
                             package.mod_settings_dependencies if name == 'mod-settings' else None)
        dependencies = dependency_reader() if dependency_reader else None
        print(f'Building {c.NAMES[name]} ({args.config})...', flush=True)
        build = c.ROOT / 'build' / ('red4ext' if name == 'red4ext' else 'plugins') / args.config.lower()
        with (logs / f'{name}-{args.config.lower()}.log').open('w') as log:
            if name == 'red4ext':
                source = c.ROOT / lock['projects'][name]['path']
                command = ['cmake', '-S', source, *configure_args(build, xwin, args.config),
                           '-DFETCHCONTENT_SOURCE_DIR_RED4EXT.SDK=' + str(c.ROOT / lock['projects']['red4ext-sdk']['path']),
                           '-DCMAKE_PROJECT_RED4ext_INCLUDE=' + str(c.ROOT / 'cmake/LoaderDependencies.cmake'),
                           '-DRED4EXT_GIT_METADATA=ON', '-DRED4EXT_EXTRA_WARNINGS=ON',
                           '-DRED4EXT_TREAT_WARNINGS_AS_ERRORS=ON', '-DFETCHCONTENT_UPDATES_DISCONNECTED=ON']
                for cmake_name, dependency in lock['loader_dependencies'].items():
                    command += ['-DFETCHCONTENT_SOURCE_DIR_' + cmake_name + '=' + str(c.ROOT / lock['dependencies'][dependency]['path'])]
                logged(command, log)
            else:
                logged(['cmake', '-S', c.ROOT, *configure_args(build, xwin, args.config)], log)
            logged(['cmake', '--build', build, '--config', args.config, '--target',
                    'all' if name == 'red4ext' else c.NAMES[name], '--parallel', str(args.jobs)], log)
            if name == 'red4ext':
                logged(['cmake', '--install', build, '--config', args.config,
                        '--prefix', build / 'install'], log)
        if patches.identity(name, require_saved=True) != before or patches.identity('red4ext-sdk', require_saved=True) != sdk:
            raise ValueError('Sources changed during the build; packages were not created.')
        if dependency_reader and dependency_reader() != dependencies:
            raise ValueError(f'{name}: dependencies changed during the build; packages were not created.')
        package.create(name, args.config)
    destination = distribution.collect(args.config, selected, args.dist_dir)
    print(f'Runtime packages are listed in {destination / "manifest.json"}')


if __name__ == '__main__':
    c.entry(main)
