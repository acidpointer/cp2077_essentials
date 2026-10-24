"""Build/run the isolated compatibility core; no downloads or Git mutations."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OLD = 'f9ad5334362a8a0815baf58f47615697485feb52'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs():
    paths = [ROOT / 'scripts/test-compatibility.py', ROOT / 'cmake/Compatibility.cmake',
             ROOT / 'cmake/toolchains/clang-cl-linux.cmake']
    for directory in ('compat', 'tests/compatibility'):
        paths.extend(p for p in (ROOT / directory).rglob('*')
                     if p.is_file() and '__pycache__' not in p.parts and
                     (p.suffix in ('.cpp', '.hpp', '.py', '.json', '.cmake') or p.name == 'CMakeLists.txt'))
    return {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(paths)}


def headers(include):
    result = hashlib.sha256()
    for path in sorted(include.rglob('*.hpp')):
        result.update(path.relative_to(include).as_posix().encode() + b'\0')
        result.update(bytes.fromhex(digest(path)))
    return result.hexdigest()


def execute(command, cwd, env, log_path, timeout):
    with log_path.open('w') as log:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=(os.name != 'nt'))
        try:
            code = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            return {'timeout_seconds': timeout}
    return {'returncode': code}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=('portable', 'win64'), default='win64')
    parser.add_argument('--out', type=Path)
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--build-only', action='store_true')
    group.add_argument('--run-only', action='store_true')
    parser.add_argument('--old-sdk', type=Path, help='Existing repository containing the pinned historical SDK object')
    parser.add_argument('--generator')
    parser.add_argument('--wine', default='wine')
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--label', default='default', help='Retain separate run records for each runtime')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if not args.label or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_' for c in args.label):
        parser.error('label must contain only letters, digits, dash or underscore')
    out = (args.out or ROOT / 'build/compat' / args.platform).resolve()
    out.mkdir(parents=True, exist_ok=True)
    manifest = out / 'build-record.json'
    env = os.environ.copy()
    cpp_inputs = inputs()
    sdk_digest = headers(ROOT / 'sources/red4ext-sdk/include')
    targets = ['compat_core_tests', 'compat_live_tests']
    if args.platform == 'win64':
        targets += ['compat_sdk_tests', 'compat_platform_tests', 'compat_function_tests', 'compat_array_tests', 'compat_registration_tests']
    suffix = '.exe' if args.platform == 'win64' else ''
    if os.name == 'nt' and args.platform == 'portable':
        parser.error('portable mode is a Unix algorithm check; use win64 on Windows')
    record = {'platform': args.platform, 'inputs': cpp_inputs, 'current_sdk_headers': sdk_digest,
              'historical_sdk_commit': OLD, 'steps': [], 'complete': False}

    def save(value, path):
        text = json.dumps(value, indent=2).replace(str(out), '<out>').replace(str(ROOT), '<repo>')
        path.write_text(text + '\n')

    if args.run_only:
        record = json.loads(manifest.read_text())
        if not record['complete'] or record['platform'] != args.platform or record['inputs'] != cpp_inputs or record['current_sdk_headers'] != sdk_digest:
            raise RuntimeError('stale/incomplete build; rebuild before executing')
        for path, expected in record['binaries'].items():
            if digest(out / path) != expected:
                raise RuntimeError(f'changed binary: {path}')
    else:
        save(record, manifest) # Invalidate any previously successful bundle first.
        configure = ['cmake', '-S', str(ROOT / 'tests/compatibility'), '-B', str(out), '-DCMAKE_BUILD_TYPE=Release']
        if args.generator:
            configure += ['-G', args.generator]
            if 'Visual Studio' in args.generator:
                configure += ['-T', 'ClangCL', '-A', 'x64']
        if args.platform == 'win64':
            repository = args.old_sdk.resolve() if args.old_sdk else ROOT / 'sources/red4ext-sdk'
            archive = subprocess.check_output(['git', '-C', str(repository), 'archive', OLD, 'include/RED4ext'])
            old = Path(tempfile.mkdtemp(prefix='headers-', dir=out))
            with tarfile.open(fileobj=io.BytesIO(archive)) as package:
                package.extractall(old, filter='data')
            old_include = old / 'include'
            if not (old_include / 'RED4ext/Callback.hpp').is_file():
                raise RuntimeError('missing historical SDK headers')
            record['historical_headers'] = headers(old_include)
            configure += ['-DCOMPAT_LEGACY_INCLUDE=' + str(old_include)]
            if os.name != 'nt':
                overlay = out / 'historical-vfs.json'
                overlay.write_text(json.dumps({'version': 0, 'case-sensitive': False, 'roots': [
                    {'type': 'file', 'name': str(p), 'external-contents': str(p)}
                    for p in sorted(old_include.rglob('*')) if p.is_file()]}))
                configure += ['-DCOMPAT_LEGACY_VFS=' + str(overlay),
                              '-DCMAKE_TOOLCHAIN_FILE=' + str(ROOT / 'cmake/toolchains/clang-cl-linux.cmake'),
                              '-DXWIN_ROOT=' + str(ROOT / 'build/toolchains/xwin')]
        elif not args.generator:
            configure += ['-DCMAKE_CXX_COMPILER=clang++']
        commands = [('configure', configure), ('build', ['cmake', '--build', str(out), '--config', 'Release', '--parallel', str(args.jobs)])]
        for label, command in commands:
            result = execute(command, ROOT, env, out / (label + '.log'), 600)
            record['steps'].append({'name': label, **result})
            save(record, manifest)
            print(label, result, flush=True)
            if result.get('returncode', 1):
                print('\n'.join((out / (label + '.log')).read_text(errors='replace').splitlines()[-45:]))
                return 1
        if inputs() != cpp_inputs or headers(ROOT / 'sources/red4ext-sdk/include') != sdk_digest:
            raise RuntimeError('inputs changed during compilation; rebuild')
        binaries = [out / 'bin' / (target + suffix) for target in targets]
        if args.platform == 'win64':
            binaries += [out / 'bin' / (name + '.dll') for name in ('legacy_guest', 'legacy_guest_unoptimized', 'array_guest', 'array_guest_unoptimized', 'registration_guest', 'registration_guest_unoptimized', 'dependency', 'initializer')]
        record['binaries'] = {p.relative_to(out).as_posix(): digest(p) for p in binaries}
        record['complete'] = True
        save(record, manifest)
    if args.build_only:
        return 0

    runner = []
    if args.platform == 'win64' and os.name != 'nt':
        wine = shutil.which(args.wine) or args.wine
        runner = [wine]
        env.update(WINEPREFIX=str(out / ('prefix-ge' if args.runtime else 'prefix-wine')),
                   WINEDEBUG='-all', DISPLAY='', WINEDLLOVERRIDES='winemenubuilder.exe=d',
                   XDG_CACHE_HOME=str(out / 'cache'))
        if args.runtime:
            runner = [str(args.runtime.resolve()), '--verb=run', '--', wine]
            env.update(PATH=str(Path(wine).parent) + os.pathsep + env.get('PATH', ''),
                       STEAM_COMPAT_APP_ID='0', SteamAppId='0', SteamGameId='0',
                       XDG_CONFIG_HOME=str(out / 'config'))
    runtime = subprocess.check_output(runner + ['--version'], env=env, text=True, timeout=30).strip() if runner else sys.platform
    results = {'runtime': runtime, 'build_record_sha256': digest(manifest), 'runs': []}
    runs = [(target, []) for target in targets]
    if args.platform == 'win64':
        runs.append(('compat_sdk_tests', ['unoptimized']))
        runs.append(('compat_array_tests', ['unoptimized']))
        runs.extend(('compat_registration_tests', [mode]) for mode in ('unoptimized', 'throw'))
        runs.extend(('compat_function_tests', [mode]) for mode in ('throw', 'persistent', 'detach-failure'))
    for target, arguments in runs:
        name = target + ('-' + '-'.join(arguments) if arguments else '')
        log_path = out / f'{args.label}-{name}.log'
        result = execute(runner + [str(out / 'bin' / (target + suffix)), *arguments], out / 'bin', env, log_path, 60)
        output = log_path.read_text(errors='replace')
        result.update(target=name, output=output)
        results['runs'].append(result)
        save(results, out / f'results-{args.label}.json')
        print(name, result.get('returncode', 'timeout'), flush=True)
        if result.get('returncode', 1) or 'PASS' not in output:
            print('\n'.join(output.splitlines()[-40:]))
            return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
